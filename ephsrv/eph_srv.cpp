// astrolog-ephd: the ephemeris WebSocket service.
//
// Implements EPHEMERIS_SERVER_PLAN.md Part I sections 2-8 and 11: a uWS App
// per event-loop thread (kernel SO_REUSEPORT balances accepts), a private
// slice of the Swiss Ephemeris thread-safe fork's context pool per loop,
// protocol version 4 (ephproto.h; EPHEMERIS_PLUGINS_PLAN.md section 3 is its
// specification, ephswiss.h the mapping of its questions to Swiss calls),
// heartbeats, and zero-configuration ephemeris path discovery. One thread at a time per swe_ctx;
// per-request configuration uses ONLY the _r scoped setters; swe_close() is
// never called and no ephemeris fallback exists (the fork's strict default;
// swe_set_ephe_fallback is deliberately never called).
//
// Sections in this file:
//   1. startup options (plan 11)
//   2. ephemeris path discovery (plan 6, 7) -- standalone, no engine code
//   3. request execution -- profiles and objects to Swiss calls, a pure
//      function answered from the loop's result cache when the same
//      question was asked of the same dataset before (eph_cache.h); LOOKUP
//   4. loop wiring -- uWS App, per-loop state, message handlers, backpressure
//   5. heartbeats -- uWS protocol pings on a 30 s idle timeout (plan 4.7)
//   6. TLS -- certificate checks at startup, reload on SIGHUP
//   7. operations -- /healthz, /readyz, /metrics, and the SIGTERM drain
//   8. main
//
// The log is one logfmt line an event (LogEvt, below the options): see
// ephsrv/deploy/README.md for the events and their keys.
//
// Every handler is a template over uWS's SSL parameter: one binary serves
// ws:// or, given --tls-cert and --tls-key, wss:// (production plan Phase 1).

#include "ephproto.h"
#include "eph_cache.h"
#include "ephswiss.h"

#include <App.h>

#include <swephexp.h>
// The internal headers, for the precession a node's frame needs (see
// RotateNodeToFixedFrame below). swephexp.h wraps itself for C++;
// these do not, so the declarations would mangle and fail to link.
extern "C" {
#include <sweph.h>
#include <swephlib.h>
}
// After those: it uses J2000, J2000_TO_J and the SSY_PLANE_* constants, which
// live in sweph.h. EPHSID_FORK picks the context-taking swi_* forms.
#define EPHSID_FORK
#include "ephsidplane.h"

#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <arpa/inet.h>
#include <csignal>
#include <ctime>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <atomic>
#include <cerrno>
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
#include <unordered_set>
#include <vector>

using uWS::WebSocket;

// ---------------------------------------------------------------------------
// 1. Startup options (plan 11)
// ---------------------------------------------------------------------------

// The limits WELCOME advertises (EPHEMERIS_PLUGINS_PLAN.md 3.4): objects,
// rows and profiles a REQUEST, rows a DATA chunk, bytes a message, and the
// matches a LOOKUP. --max-cells sets maxCells.
static const uint32_t kMaxObjs = 64, kMaxRows = 20000, kMaxChunkRows = 500;
static const uint32_t kMaxPayload = 4u * 1024u * 1024u;
static const uint32_t kMaxCellsDefault = 100000;
static const uint8_t kMaxProfiles = 16;
static const uint16_t kLookupMax = 32;
static const int kErrMax = eph::kErrDraining;   // the highest A.19 code
// The extra columns (A.10) served: the ayanamsa applied and the delta T
// used. Not sigma (Swiss has none) nor light time.
static const uint32_t kColumnsServed = eph::kColAyanamsa | eph::kColDeltaT;

// Log levels, most severe first: a line is written when its level is at or
// above --log-level's.
enum LogLevel { kLogError = 0, kLogWarn = 1, kLogInfo = 2, kLogDebug = 3 };
static const char *const kLogLevelNames[] = {"error", "warn", "info", "debug"};

struct Options {
  uint16_t port = eph::kDefaultPort;
  int threads = (int)std::thread::hardware_concurrency();
  uint32_t cacheMb = 256;      // result cache, TOTAL across loops; 0 disables
  uint32_t maxCells = kMaxCellsDefault;  // objects x rows per REQUEST
  std::string ephe;            // --ephe, may be ';'-joined
  int logLevel = kLogInfo;     // --log-level; --verbose is debug
  bool logContents = false;    // --log-contents: what each REQUEST asked
  std::string bind;            // --bind: one address; empty is every interface
  std::string tlsCert;         // --tls-cert: PEM, the chain after the leaf
  std::string tlsKey;          // --tls-key: PEM private key
  uint32_t drainSeconds = 10;  // --drain-seconds: SIGTERM's grace period
  // Limits (production plan Phase 3). 0 disables each.
  uint32_t maxConns = 10000;       // --max-conns: WebSockets open, in total
  uint32_t maxConnsPerAddr = 64;   // --max-conns-per-ip
  uint32_t cellsPerSec = 10000;    // --cells-per-sec: each address's (or
                                   // token's) refill; the burst is --max-cells
  uint32_t helloSeconds = 10;      // --hello-seconds: HELLO deadline
  std::string tokensFile;          // --tokens: accepted tokens, one a line
  bool requireToken = false;       // --require-token
  bool Tls() const { return !tlsCert.empty(); }
};

static Options gOpt;

// ---------------------------------------------------------------------------
// The log: one logfmt line an event, to stdout, flushed as written.
//
//   ts=2026-09-17T12:34:56.789Z level=info evt=conn.open conn=17 loop=3 addr=203.0.113.9
//
// ts, level and evt lead every line, in that order; the event's own keys
// follow. A value holding a space, '=', '"', '\\' or a control character is
// quoted, with \" \\ \n \t escapes; an empty one is "". Levels:
//   error  the server could not do something: a fatal start, a reload that
//          failed, an ERROR 4
//   warn   a client refused or cut off (connection caps, a HELLO refused
//          or never sent, every other ERROR), a degraded start, a drain
//          that had to cut answers off
//   info   the default: lifecycle, and every connection, HELLO and REQUEST
//   debug  per-loop detail, backpressure stalls, the HTTP routes
// Never logged at any level: a token. Logged only under --log-contents,
// which says so at startup: what a REQUEST asked -- instants, bodies,
// sidereal and topocentric settings (production plan decision 0.3).
// Swiss's per-row error text names the instant and is never logged.
// ---------------------------------------------------------------------------
static bool FLog(int level) { return level <= gOpt.logLevel; }

class LogEvt {
 public:
  LogEvt(int level, const char *evt) : fOn(FLog(level)) {
    if (!fOn) return;
    timespec ts {};
    clock_gettime(CLOCK_REALTIME, &ts);
    tm t {};
    gmtime_r(&ts.tv_sec, &t);
    char sz[80];
    snprintf(sz, sizeof(sz), "ts=%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ level=%s evt=",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min,
             t.tm_sec, ts.tv_nsec / 1000000L, kLogLevelNames[level]);
    line = sz;
    line += evt;
  }
  // One write under stdio's lock: loops log from their own threads, and a
  // line is never interleaved with another.
  ~LogEvt() {
    if (!fOn) return;
    line += '\n';
    flockfile(stdout);
    fwrite(line.data(), 1, line.size(), stdout);
    fflush(stdout);
    funlockfile(stdout);
  }
  // A builder: keys only, no header, never written -- Take() hands the text
  // to a line that will be (the REQUEST's contents, kept on its Stream).
  LogEvt() : fOn(true) {}
  std::string Take() {
    fOn = false;
    return std::move(line);
  }
  LogEvt &Raw(const std::string &keys) {
    if (fOn) line += keys;
    return *this;
  }
  LogEvt(const LogEvt &) = delete;
  LogEvt &operator=(const LogEvt &) = delete;

  bool On() const { return fOn; }
  LogEvt &U(const char *key, uint64_t v) {
    if (fOn) Key(key), line += std::to_string(v);
    return *this;
  }
  LogEvt &I(const char *key, int64_t v) {
    if (fOn) Key(key), line += std::to_string(v);
    return *this;
  }
  LogEvt &F(const char *key, double v, int prec) {
    if (fOn) {
      char sz[64];
      snprintf(sz, sizeof(sz), "%.*f", prec, v);
      Key(key), line += sz;
    }
    return *this;
  }
  LogEvt &X(const char *key, uint64_t v) {
    if (fOn) {
      char sz[24];
      snprintf(sz, sizeof(sz), "0x%" PRIx64, v);
      Key(key), line += sz;
    }
    return *this;
  }
  LogEvt &B(const char *key, bool v) { return U(key, v ? 1 : 0); }
  LogEvt &S(const char *key, std::string_view v) {
    if (!fOn) return *this;
    Key(key);
    bool fQuote = v.empty();
    for (unsigned char ch : v)
      if (ch <= ' ' || ch == '=' || ch == '"' || ch == '\\' || ch == 0x7f) {
        fQuote = true;
        break;
      }
    if (!fQuote) {
      line += v;
      return *this;
    }
    line += '"';
    for (unsigned char ch : v) {
      if (ch == '"' || ch == '\\') line += '\\', line += (char)ch;
      else if (ch == '\n') line += "\\n";
      else if (ch == '\t') line += "\\t";
      else if (ch < ' ' || ch == 0x7f) {
        char sz[8];
        snprintf(sz, sizeof(sz), "\\x%02x", ch);
        line += sz;
      } else line += (char)ch;
    }
    line += '"';
    return *this;
  }
  __attribute__((format(printf, 2, 3)))
  LogEvt &Msg(const char *fmt, ...) {
    if (!fOn) return *this;
    char sz[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(sz, sizeof(sz), fmt, ap);
    va_end(ap);
    return S("msg", sz);
  }
  // A connection's identity: its id, loop and peer address. A template so
  // Conn, defined below, need not be complete here.
  template <class C> LogEvt &Conn(const C *c) {
    return U("conn", c->id).I("loop", c->loop).S("addr", c->addr);
  }

 private:
  void Key(const char *key) {
    line += ' ';
    line += key;
    line += '=';
  }
  bool fOn;
  std::string line;
};

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

static const char kUsage[] =
  "usage: astrolog-ephd [--port N] [--bind ADDR] [--ephe path] [--threads N]\n"
  "                     [--cache-mb N] [--max-cells N]\n"
  "                     [--log-level error|warn|info|debug] [--verbose]\n"
  "                     [--log-contents]\n"
  "                     [--tls-cert FILE --tls-key FILE] [--drain-seconds N]\n"
  "  --tls-cert/--tls-key  serve wss:// (PEM; the chain after the leaf);\n"
  "                        SIGHUP reloads both without dropping connections\n"
  "  --drain-seconds N     SIGTERM/SIGINT: stop accepting, let answers in\n"
  "                        flight finish for up to N s (default 10), close\n"
  "                        every connection 1001, exit 0; a second signal\n"
  "                        exits at once\n"
  "                     [--max-conns N] [--max-conns-per-ip N]\n"
  "                     [--cells-per-sec N] [--hello-seconds N]\n"
  "                     [--tokens FILE [--require-token]]\n"
  "  --max-conns N         WebSockets open in total (default 10000; 0 = none)\n"
  "  --max-conns-per-ip N  from one address (default 64; 0 = none)\n"
  "  --cells-per-sec N     each address's compute budget, refilling at N\n"
  "                        cells a second up to --max-cells (default 10000;\n"
  "                        0 = none); a token has its own budget\n"
  "  --hello-seconds N     close a connection with no HELLO after N s (10)\n"
  "  --tokens FILE         accepted tokens, one a line; --require-token\n"
  "                        refuses a HELLO without one of them\n"
  "  --log-level L         logfmt lines at L and above (default info: every\n"
  "                        connection, HELLO and REQUEST); --verbose is debug\n"
  "  --log-contents        log what each REQUEST asks -- instants, bodies,\n"
  "                        settings -- for debugging; off by default, and a\n"
  "                        public server should leave it off\n"
  "  GET /healthz /readyz /metrics on the same port (Prometheus text)\n";

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
    } else if (strcmp(a, "--max-cells") == 0) {
      const char *val;
      if (!needValue(&val) || !parseU(val, &v) || v == 0 || v > UINT32_MAX)
        return false;
      opt->maxCells = (uint32_t)v;
    } else if (strcmp(a, "--ephe") == 0) {
      const char *val;
      if (!needValue(&val)) return false;
      opt->ephe = val;
    } else if (strcmp(a, "--verbose") == 0) {
      opt->logLevel = kLogDebug;
    } else if (strcmp(a, "--log-level") == 0) {
      const char *val;
      if (!needValue(&val)) return false;
      int l = 0;
      while (l <= kLogDebug && strcmp(val, kLogLevelNames[l]) != 0) l++;
      if (l > kLogDebug) return false;
      opt->logLevel = l;
    } else if (strcmp(a, "--log-contents") == 0) {
      opt->logContents = true;
    } else if (strcmp(a, "--bind") == 0) {
      const char *val;
      if (!needValue(&val) || !*val) return false;
      opt->bind = val;
    } else if (strcmp(a, "--drain-seconds") == 0) {
      const char *val;
      if (!needValue(&val) || !parseU(val, &v) || v > 3600) return false;
      opt->drainSeconds = (uint32_t)v;
    } else if (strcmp(a, "--max-conns") == 0 ||
               strcmp(a, "--max-conns-per-ip") == 0 ||
               strcmp(a, "--cells-per-sec") == 0 ||
               strcmp(a, "--hello-seconds") == 0) {
      const char *val;
      if (!needValue(&val) || !parseU(val, &v) || v > UINT32_MAX) return false;
      uint32_t *pv = strcmp(a, "--max-conns") == 0 ? &opt->maxConns :
                     strcmp(a, "--max-conns-per-ip") == 0 ? &opt->maxConnsPerAddr :
                     strcmp(a, "--cells-per-sec") == 0 ? &opt->cellsPerSec :
                     &opt->helloSeconds;
      *pv = (uint32_t)v;
    } else if (strcmp(a, "--tokens") == 0) {
      const char *val;
      if (!needValue(&val) || !*val) return false;
      opt->tokensFile = val;
    } else if (strcmp(a, "--require-token") == 0) {
      opt->requireToken = true;
    } else if (strcmp(a, "--tls-cert") == 0) {
      const char *val;
      if (!needValue(&val) || !*val) return false;
      opt->tlsCert = val;
    } else if (strcmp(a, "--tls-key") == 0) {
      const char *val;
      if (!needValue(&val) || !*val) return false;
      opt->tlsKey = val;
    } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
      printf("%s", kUsage);
      exit(0);
    } else {
      return false;
    }
  }
  // A certificate without its key, or a key without its certificate, is a
  // mistake, not a request for plain text.
  if (opt->tlsCert.empty() != opt->tlsKey.empty()) return false;
  // Requiring a token with no list of tokens would refuse everyone.
  if (opt->requireToken && opt->tokensFile.empty()) return false;
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

  // An explicit --ephe is the whole search: the operator said where the
  // files are. Falling through to the environment, the settings file and
  // the executable's directory found SOME ephemeris when the one named had
  // none, and a gate pointing the server at a directory with no files was
  // silently answered from ./ephem (EPHEMERIS_REVIEW.md T3). A directory
  // with nothing in it is "<none found>", and the log says so.
  if (gOpt.ephe.empty()) {

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

  }   // gOpt.ephe.empty()

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
// 3. Request execution (EPHEMERIS_PLUGINS_PLAN.md 3.4-3.5, Appendix B)
//
// A request is a pure function of its question block: look it up in the
// loop's result cache under datasetId + those bytes (3.7), else map each
// object through its profile to a Swiss call (ephswiss.h), make that call
// for every row on the loop's one context, and store the answer. A row that
// fails is NaN in every column and never fails the object; an object whose
// rows all fail carries its error in META and never fails the request
// (3.5). Per-request configuration -- sidereal mode, topocentric site --
// uses only the fork's _r scoped setters, set per object, since two objects
// of one request may sit in different profiles.
// ---------------------------------------------------------------------------

struct LoopCtx;   // 4.
struct ObjPrep;   // 3.

// A request being computed, in blocks of rows across loop turns (3.4
// CANCEL). Everything it needs outlives the callback that parsed the
// REQUEST, so the question is held by value.
//
// Nothing here ever reaches the result cache until the last row is computed:
// a cancelled request caches nothing, or a later identical question hits half
// an answer (3.4).
struct Work {
  eph::Request req;
  std::string key;                             // datasetId + the question block
  std::shared_ptr<eph::CacheEntry> entry;      // filled in place
  std::vector<ObjPrep> prep;                   // one per object, after PrepareObject
  bool fPrepared = false;
  uint32_t iObj = 0;                           // the object being computed
  uint32_t rowNext = 0;                        // its rows computed so far
  double computeMs = 0.0;                      // summed over its blocks
};

// One answer awaiting, computing, or streaming to its client.
struct Stream {
  uint32_t requestId = 0;
  uint8_t precision = eph::kPrecF64;
  bool fIgnoredExt = false;     // DATA chunkFlags bit 1
  uint32_t nObj = 0, nTimeRows = 0, chunkRows = 0;
  // Set while the answer is still being computed; null once `result` holds a
  // complete answer. A stream with work pending sends nothing: DATA chunk 0
  // carries every object's META, and META counts the rows that computed, so
  // it cannot be written before the last row is known (3.4).
  std::unique_ptr<Work> work;
  uint32_t nextRow = 0;         // next row to send
  uint32_t chunkIndex = 0;
  // The computed answer, shared with the loop's result cache: a hit hands
  // out the cached entry, a miss the one just computed and inserted. The
  // stream keeps the entry alive however the cache turns over meanwhile.
  std::shared_ptr<const eph::CacheEntry> result;
  std::vector<uint8_t> buf;     // chunk scratch, reused
  // For the request's log line, written as its last chunk goes out.
  std::chrono::steady_clock::time_point tReq;  // REQUEST received
  double computeMs = 0.0;       // 0 on a cache hit
  bool fHit = false;
  uint32_t stalls = 0;          // times it waited for the client to read
  uint64_t bytes = 0;           // DATA bytes sent
  uint32_t nProfiles = 0;
  uint8_t timeMode = 0, timeScale = 0, priority = 0;
  // 3.4: advisory, recorded and logged. This server has one strategy --
  // samples, computed now -- so there is no cheaper one to choose, and a
  // request is never failed for the deadline it states.
  uint32_t deadlineMs = 0;
  std::string contents;         // --log-contents only: what was asked
  bool fLogged = false;
};

struct Conn {
  std::deque<Stream> out;       // answers computing or streaming, FIFO
  // proto is the session's version, fixed by the first HELLO; 0 before it,
  // when only HELLO and PING are served. addr is the peer's address as
  // text; budget is what the cell budget is keyed on -- "t:" and the token
  // when HELLO gave a known one, "a:" and the address otherwise.
  uint8_t proto = 0;
  uint32_t clientCaps = 0;
  std::chrono::steady_clock::time_point tOpen = std::chrono::steady_clock::now();
  std::string addr;
  std::string budget;
  bool fCountedAddr = false;    // holds a place in gConnsByAddr
  // For the log: an id unique for the process's life, the loop, what HELLO
  // said the client is, and the connection's totals for its close line.
  uint64_t id = 0;
  int loop = -1;
  std::string client;           // HELLO's clientName
  uint32_t hellos = 0;
  uint64_t reqs = 0, hits = 0, cells = 0, errors = 0, bytesOut = 0;
  uint64_t lookups = 0, cancels = 0;

  // The place is released when the Conn goes, however it goes -- a client
  // that drops between the upgrade and open never reaches the close handler.
  // Moving hands the place over, since uWS moves the upgrade's Conn into the
  // socket and then destroys the original.
  Conn() = default;
  Conn(Conn &&o) noexcept
    : out(std::move(o.out)), proto(o.proto), clientCaps(o.clientCaps), tOpen(o.tOpen),
      addr(std::move(o.addr)), budget(std::move(o.budget)),
      fCountedAddr(o.fCountedAddr), id(o.id), loop(o.loop),
      client(std::move(o.client)), hellos(o.hellos), reqs(o.reqs),
      hits(o.hits), cells(o.cells), errors(o.errors), bytesOut(o.bytesOut),
      lookups(o.lookups), cancels(o.cancels) {
    o.fCountedAddr = false;
  }
  Conn &operator=(Conn &&) = delete;
  ~Conn();
};

// Answers a connection may have computed and not yet read. A client that
// sends requests and never reads used to make the server compute and hold
// every one -- 60 MB of columns for each 64-body 20000-row window, kept
// alive by its stream after the cache let it go -- so past this many the
// next request is refused with ERROR 9 (busy, retryable) before anything
// is computed.
static const size_t kMaxQueuedStreams = 4;

// Bytes a connection may have buffered unsent before its DATA chunks wait
// for drain. Applied here rather than as uWS's maxBackpressure; see
// WireApp.
static const unsigned kStreamBackpressure = 4 * 1024 * 1024;

// Server name for WELCOME: 2.0 is the first to speak protocol 4.
static const char kServerVersion[] = "astrolog-ephd/2.0";

// The one source this server has, META's sourceIdx 0: the shape 3.4 gives,
// <engine> | <ephemeris> | <model>, filled in by BuildWelcome().
static std::string gSource = "astrolog-ephd 2.0 | Swiss Ephemeris | files";

// WELCOME's payload, the same for every connection: built once in main()
// from the limits and the dataset, after the ephemeris path is known.
static std::string gDatasetId;
static std::string gEngine;
static std::vector<uint8_t> gWelcome;
static eph::Capabilities gCaps;

// Swiss's text is Latin-1 in places (asteroid names from seasnam.txt), and
// a str8 must be UTF-8 without controls (3.1): the bytes that are not are
// '?'. At most 255 bytes, cut on a character boundary.
static std::string WireText(const char *sz) {
  std::string s;
  for (const unsigned char *p = (const unsigned char *)sz; *p && s.size() < 255; p++)
    s += (*p >= 0x20 && *p < 0x7F) ? (char)*p : '?';
  return s;
}

// A.17 and its text, from a Swiss error message. 3.8 covers META's errText
// as well as ERROR's: it never quotes instants, places or coordinates, and
// Swiss's messages routinely name the instant ("jd 2597700.5 outside
// ephemeris range"). So the class is read off the message and the text is
// this server's own fixed phrase for that class, plus the file name where
// Swiss named one (a file name is not a request's contents).
static uint16_t ObjErrOf(const char *serr, bool fStar, std::string *pText) {
  uint16_t code;
  const char *szPhrase;
  if (strstr(serr, "outside") || strstr(serr, "range") || strstr(serr, "beyond")) {
    code = eph::kOErrCoverage;
    szPhrase = "the instant is outside this ephemeris's coverage";
  } else if (fStar && (strstr(serr, "not found") || strstr(serr, "could not find"))) {
    code = eph::kOErrUnknownBody;
    szPhrase = "no star of that name in the catalogue";
  } else if (strstr(serr, "not found") || strstr(serr, "file") || strstr(serr, "open")) {
    code = eph::kOErrDataMissing;
    szPhrase = "the ephemeris file for this body is not on the server's path";
  } else if (strstr(serr, "not implemented") || strstr(serr, "illegal")) {
    code = eph::kOErrUnsupported;
    szPhrase = "this engine does not compute this body or point";
  } else {
    code = eph::kOErrNumerical;
    szPhrase = "the engine could not compute this object";
  }
  *pText = szPhrase;
  // The file Swiss named, if it named one: se00433.se1, sepl_18.se1 and the
  // like. Nothing else of the message is carried.
  const char *q = strstr(serr, ".se1");
  if (q != nullptr) {
    const char *start = q;
    while (start > serr && (isalnum((unsigned char)start[-1]) || start[-1] == '_')) start--;
    *pText += " (";
    pText->append(start, (size_t)(q + 4 - start));
    *pText += ")";
  }
  return code;
}

// ---- LOOKUP's catalogue (3.4) ------------------------------------------------
//
// A modest resolver: the bodies Swiss serves by name, the Moon's named
// points, the A.15 hypotheticals, numbered asteroids by number, and fixed
// stars through Swiss's own catalogue. Only what this server can answer is
// listed, so every match is something a REQUEST can ask.
struct NamedObject {
  const char *name;          // canonical
  const char *aliases;       // '|'-separated, may be ""
  const char *designation;   // "" when none
  uint8_t kind;
  int32_t naif;
  uint8_t point, method;
  const char *token;         // hypotheticals
};

static const NamedObject kCatalogue[] = {
  {"Sun", "Sol", "", eph::kObjBody, 10, 0, 0, nullptr},
  {"Moon", "Luna", "", eph::kObjBody, 301, 0, 0, nullptr},
  {"Mercury", "", "", eph::kObjBody, 199, 0, 0, nullptr},
  {"Venus", "", "", eph::kObjBody, 299, 0, 0, nullptr},
  {"Earth", "", "", eph::kObjBody, 399, 0, 0, nullptr},
  {"Mars", "", "", eph::kObjBody, 4, 0, 0, nullptr},
  {"Jupiter", "", "", eph::kObjBody, 5, 0, 0, nullptr},
  {"Saturn", "", "", eph::kObjBody, 6, 0, 0, nullptr},
  {"Uranus", "", "", eph::kObjBody, 7, 0, 0, nullptr},
  {"Neptune", "", "", eph::kObjBody, 8, 0, 0, nullptr},
  {"Pluto", "", "134340", eph::kObjBody, 9, 0, 0, nullptr},
  {"Ceres", "", "1", eph::kObjBody, 20000001, 0, 0, nullptr},
  {"Pallas", "Pallas Athene", "2", eph::kObjBody, 20000002, 0, 0, nullptr},
  {"Juno", "", "3", eph::kObjBody, 20000003, 0, 0, nullptr},
  {"Vesta", "", "4", eph::kObjBody, 20000004, 0, 0, nullptr},
  {"Chiron", "", "2060", eph::kObjBody, 20002060, 0, 0, nullptr},
  {"Pholus", "", "5145", eph::kObjBody, 20005145, 0, 0, nullptr},
  {"Lilith", "", "1181", eph::kObjBody, 20001181, 0, 0, nullptr},
  {"Moon mean node", "Mean Node|North Node|Rahu", "", eph::kObjOrbitPoint, 301,
   eph::kPtAscNode, eph::kMethMean, nullptr},
  {"Moon true node", "True Node", "", eph::kObjOrbitPoint, 301, eph::kPtAscNode,
   eph::kMethOsculating, nullptr},
  {"Moon mean descending node", "South Node|Ketu", "", eph::kObjOrbitPoint, 301,
   eph::kPtDescNode, eph::kMethMean, nullptr},
  {"Moon mean apogee", "Lilith|Black Moon Lilith|Mean Apogee", "", eph::kObjOrbitPoint, 301,
   eph::kPtApo, eph::kMethMean, nullptr},
  {"Moon osculating apogee", "True Lilith|Osculating Apogee", "", eph::kObjOrbitPoint, 301,
   eph::kPtApo, eph::kMethOsculating, nullptr},
  {"Moon interpolated apogee", "Natural Apogee|Interpolated Apogee", "", eph::kObjOrbitPoint, 301,
   eph::kPtApo, eph::kMethInterpolated, nullptr},
  {"Moon interpolated perigee", "Natural Perigee|Priapus|Interpolated Perigee", "",
   eph::kObjOrbitPoint, 301, eph::kPtPeri, eph::kMethInterpolated, nullptr},
  {"Cupido", "", "", eph::kObjHypothetical, 0, 0, 0, "cupido"},
  {"Hades", "", "", eph::kObjHypothetical, 0, 0, 0, "hades"},
  {"Zeus", "", "", eph::kObjHypothetical, 0, 0, 0, "zeus"},
  {"Kronos", "", "", eph::kObjHypothetical, 0, 0, 0, "kronos"},
  {"Apollon", "", "", eph::kObjHypothetical, 0, 0, 0, "apollon"},
  {"Admetos", "", "", eph::kObjHypothetical, 0, 0, 0, "admetos"},
  {"Vulcanus", "", "", eph::kObjHypothetical, 0, 0, 0, "vulcanus"},
  {"Poseidon", "", "", eph::kObjHypothetical, 0, 0, 0, "poseidon"},
  {"Isis-Transpluto", "Transpluto|Isis", "", eph::kObjHypothetical, 0, 0, 0, "isis-transpluto"},
  {"Nibiru", "", "", eph::kObjHypothetical, 0, 0, 0, "nibiru"},
  {"Harrington", "", "", eph::kObjHypothetical, 0, 0, 0, "harrington"},
  {"Leverrier's Neptune", "", "", eph::kObjHypothetical, 0, 0, 0, "neptune-leverrier"},
  {"Adams's Neptune", "", "", eph::kObjHypothetical, 0, 0, 0, "neptune-adams"},
  {"Lowell's Pluto", "", "", eph::kObjHypothetical, 0, 0, 0, "pluto-lowell"},
  {"Pickering's Pluto", "", "", eph::kObjHypothetical, 0, 0, 0, "pluto-pickering"},
  {"Vulcan", "", "", eph::kObjHypothetical, 0, 0, 0, "vulcan"},
  {"White Moon", "Selena", "", eph::kObjHypothetical, 0, 0, 0, "white-moon"},
  {"Proserpina", "", "", eph::kObjHypothetical, 0, 0, 0, "proserpina"},
  {"Waldemath", "Dark Moon|Dark Moon Lilith", "", eph::kObjHypothetical, 0, 0, 0, "waldemath"},
};

static bool EqNoCase(const std::string &a, const char *b, size_t n) {
  if (a.size() != n) return false;
  for (size_t i = 0; i < n; i++)
    if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
  return true;
}
static bool PrefixNoCase(const std::string &q, const char *b, size_t n) {
  if (q.size() > n) return false;
  for (size_t i = 0; i < q.size(); i++)
    if (tolower((unsigned char)q[i]) != tolower((unsigned char)b[i])) return false;
  return true;
}

static eph::Object ObjectOf(const NamedObject &n) {
  eph::Object o;
  o.kind = n.kind;
  o.naif = n.naif;
  o.point = n.point;
  o.method = n.method;
  if (n.token) o.name = n.token;
  return o;
}

// The matches for a query, best first (3.4 LOOKUP_RESULT order: quality,
// then catalogue order), at most cMax; *pfMore when more exist. flags as
// LOOKUP's: bit 0 prefix, bit 1 hypotheticals, bit 2 stars.
static void Resolve(swe_ctx *ctx, const std::string &query, uint8_t flags, size_t cMax,
                    std::vector<eph::Match> *out, bool *pfMore) {
  std::vector<eph::Match> byQuality[3];
  auto add = [&](int q, const eph::Object &o, const std::string &name, const char *desig) {
    eph::Match m;
    m.quality = (uint8_t)q;
    m.sourceIdx = 0;
    m.obj = o;
    m.canonicalName = name;
    m.designation = desig ? desig : "";
    byQuality[q].push_back(std::move(m));
  };
  bool fDigits = !query.empty() && query.size() <= 9;
  for (char ch : query) fDigits = fDigits && ch >= '0' && ch <= '9';
  bool fCatalogued = false;
  for (const NamedObject &n : kCatalogue) {
    if (n.kind == eph::kObjHypothetical && !(flags & 2)) continue;
    int q = -1;
    if (EqNoCase(query, n.name, strlen(n.name))) q = 0;
    else if (*n.designation && EqNoCase(query, n.designation, strlen(n.designation))) q = 1;
    for (const char *p = n.aliases; q < 0 && *p;) {
      const char *bar = strchr(p, '|');
      size_t len = bar ? (size_t)(bar - p) : strlen(p);
      if (EqNoCase(query, p, len)) q = 1;
      p = bar ? bar + 1 : p + len;
    }
    if (q < 0 && (flags & 1)) {
      if (PrefixNoCase(query, n.name, strlen(n.name))) q = 2;
      for (const char *p = n.aliases; q < 0 && *p;) {
        const char *bar = strchr(p, '|');
        size_t len = bar ? (size_t)(bar - p) : strlen(p);
        if (PrefixNoCase(query, p, len)) q = 2;
        p = bar ? bar + 1 : p + len;
      }
    }
    if (q < 0) continue;
    if (q == 1 && *n.designation && EqNoCase(query, n.designation, strlen(n.designation)))
      fCatalogued = true;
    add(q, ObjectOf(n), n.name, n.designation);
  }
  if (fDigits && !fCatalogued) {
    long num = atol(query.c_str());
    if (num > 0 && (int64_t)SE_AST_OFFSET + num <= (0x7FFFFFFF - 9099) / 100) {
      eph::Object o;
      o.kind = eph::kObjBody;
      o.naif = 20000000 + (int32_t)num;
      char nm[AS_MAXCH] = "";
      swe_get_planet_name_r(ctx, SE_AST_OFFSET + (int)num, nm);
      add(1, o, WireText(nm), query.c_str());
    }
  }
  if ((flags & 4) && !fDigits && query.size() < SE_MAX_STNAME - 1) {
    char star[SE_MAX_STNAME * 2] = "", serr[AS_MAXCH] = "";
    double xx[6];
    snprintf(star, sizeof(star), "%s", query.c_str());
    // Swiss's catalogue reads names with ',' as "traditional,Bayer" and a
    // leading ',' as a designation; a query with its own ',' is left to it.
    if (swe_fixstar2_r(ctx, star, 2451545.0, SEFLG_SWIEPH, xx, serr) >= 0) {
      eph::Object o;
      o.kind = eph::kObjStar;
      o.name = query;
      add(PrefixNoCase(query, star, strlen(star)) ? 0 : 1, o, WireText(star), nullptr);
    }
  }
  out->clear();
  *pfMore = false;
  for (auto &list : byQuality)
    for (eph::Match &m : list) {
      if (out->size() >= cMax) { *pfMore = true; return; }
      out->push_back(std::move(m));
    }
}

// Kind 5: "resolved as an exact LOOKUP" (3.5) -- quality 0 or 1, the
// hypotheticals included, stars not. More than one is error 6, none 1.
static uint16_t ResolveDesignation(swe_ctx *ctx, const eph::Object &o, eph::Object *pout,
                                   std::string *pwhy) {
  std::vector<eph::Match> m;
  bool fMore;
  Resolve(ctx, o.name, 2, 8, &m, &fMore);
  while (!m.empty() && m.back().quality > 1) m.pop_back();
  if (m.empty()) { *pwhy = "designation not found"; return eph::kOErrUnknownBody; }
  if (m.size() > 1) { *pwhy = "designation names more than one object"; return eph::kOErrAmbiguous; }
  *pout = m[0].obj;
  pout->profile = o.profile;
  return eph::kOErrNone;
}

// What one object of a request resolved to, kept between the blocks of rows
// it is computed in: a request is answered across loop turns, so an object's
// setup happens once and the row loop is re-entered many times.
struct ObjPrep {
  eph::swiss::SwissCall c;
  bool fDead = false;    // failed before a single row; its rows are NaN
  bool fNamed = false;   // META's name has been filled from the first good row
};

// The per-object setup: the designation resolved, the body mapped to a Swiss
// call, and the object-level META. Everything here is cheap and happens once,
// before any row of any object, because chunk 0 carries every object's META.
static void PrepareObject(swe_ctx *ctx, const eph::Request &req, uint32_t iObj,
                          eph::CacheEntry *e, ObjPrep *prep) {
  const eph::Profile &pf = req.profiles[req.objs[iObj].profile];
  eph::Meta &m = e->meta[iObj];
  const uint32_t nTime = e->nTimeRows, nCols = e->nCols;
  double *dst0 = e->cols.data() + (size_t)iObj * nTime * nCols;
  eph::Object ob = req.objs[iObj];
  std::string why;
  uint16_t err = eph::kOErrNone;

  m.sourceIdx = 0;
  if (ob.kind == eph::kObjDesignation)
    err = ResolveDesignation(ctx, ob, &ob, &why);
  if (!err && ob.kind == eph::kObjElements) {
    err = eph::kOErrUnsupported;
    why = "orbital elements are not served by this server";
  }
  if (!err)
    // nNative 0: this end answers the wire, and the wire has no native ids
    // (ephswiss.h). Astrolog's own local plugin is the caller that passes one.
    err = eph::swiss::MapObject(ob, 0, pf, req.timeScale, req.deltaTSec, SEFLG_SWIEPH,
                                &prep->c, &why);
  if (err) {
    for (size_t i = 0; i < (size_t)nTime * nCols; i++) dst0[i] = NAN;
    m.errCode = err;
    m.errText = WireText(why.c_str());
    m.firstFailedRow = 0;
    m.name = WireText(ob.name.c_str());
    prep->fDead = true;
    return;
  }
  m.resolvedNaif = prep->c.resolvedNaif;
  // 3.4 META corrApplied: the terms this call can apply to this object for
  // this observer -- the capability set, independent of the mask the
  // request asked for (a request for true positions still reports them;
  // what it asked stays the client's own knowledge).
  m.corrApplied = eph::swiss::CorrectionsLive(prep->c, pf.observer);
  if (prep->c.fApproximated) m.flags |= eph::kMetaApproximated;
  if (!pf.speeds) m.flags |= eph::kMetaNoSpeeds;
  // 3.5a: Swiss's rates are its own analytic derivatives and differ from
  // central differences of its positions by more than the tolerance (the
  // rates-bound capability says by how much), so every object with speeds
  // says so.
  if (pf.speeds) m.flags |= eph::kMetaRatesApprox;
}

// ---- 3.5a's fixed sidereal planes, computed here rather than by Swiss ----
//
// A sidereal zodiac's zero point is the direction the zodiac NAMES -- the
// direction at longitude A0 on the mean ecliptic of t0 -- projected onto the
// plane, with longitude counted from that projection. Swiss instead carries
// the equinox of t0 onto the plane and walks the ayanamsa there, which gives
// plane 2 an origin that is not the zodiac's: +31.47" out under Fagan/Bradley,
// +30.39" under Lahiri, +27.58" under Raman. It depends on the ZODIAC, and a
// plane does not know which ayanamsa was asked for -- so the origin is tied to
// the equinox rather than to the sky, which is the argument that settled it.
// EPHEMERIS_ACCURACY_REGISTRY.md 4.1; tools/sidplane-origin.py measures it.
//
// Swiss also declines the plane bits outright for 16 of the 47 registry
// ayanamsas and returns the plane-0 row bit-identically, with no error. Both
// defects go together here, because taking the transform in-house is what
// fixes them: it no longer matters whether Swiss would have honoured the bits.
//
// NOT a shortcut that was tried and rejected: building the zero point from
// a(t), the ayanamsa at the REQUEST's instant, on the ecliptic of date. That
// needs no anchor and would be uniform over every mode, but it uses the zero
// point's PROJECTION onto the ecliptic of date rather than the zero point, and
// the two ecliptics have separated by a fifth of a degree since Lahiri's t0.
// Measured difference in plane 2's origin: 30.75" for Lahiri at J2000, 31.34"
// for Raman, 10.66" for Fagan/Bradley -- the same size as the effect being
// fixed. Ephemeris Prometheia builds it from A0 and t0 as well.

// Once per object: the anchor, the ayanamsa there, and plane 2's origin.
// The protocol-side wrapper: the shared EPHSIDPLANE plus the two bits that
// are this server's policy rather than arithmetic -- whether a fixed plane
// was asked for at all, and whether the zodiac's anchor could be built.
struct SidPlaneReq {
  bool fActive = false;
  uint16_t err = 0;
  // 3.5a's anchors sentence: the anchor is taken at its TRUE position, no
  // aberration and no deflection. Swiss takes it apparent, which makes the
  // zero point of a star- or galaxy-anchored zodiac carry the Earth's own
  // motion -- 40.179" peak to peak over a year for Spica. Set for the twelve
  // modes whose anchor is a direction rather than an epoch; the correction is
  // a scalar on longitude, since an ayanamsa is a rotation about the ecliptic
  // pole and moves no latitude.
  bool fTrueAnchor = false;
  // 3.5a's instant-defined clause: a zodiac with no anchor epoch has its zero
  // point at sidereal longitude 0 on the ecliptic OF THE REQUEST INSTANT, so
  // plane 2's origin is not a per-object constant -- it moves along a grid and
  // has to be rebuilt for every row.
  bool fPerRow = false;
  EPHSIDPLANE p{};
};

static SidPlaneReq PrepareSidPlane(swe_ctx *ctx, const eph::swiss::SwissCall &c) {
  SidPlaneReq sp;
  if (!c.fSidereal) return sp;
  {
    // Does this zodiac's zero point come from a DIRECTION rather than an
    // epoch? Swiss marks those with t0 = 0 in its ayanamsa table, and they
    // are the ones the anchors sentence is about, on every plane including
    // plane 0. Decided here so it is one test in one place.
    const int32 m0 = c.sidMode & 0xFF;
    sp.fTrueAnchor = m0 != SE_SIDM_USER && m0 >= 0 && m0 < SE_NSIDM_PREDEF &&
      ayanamsa[m0].t0 == 0.0;
  }
  const bool f2 = (c.sidMode & SE_SIDBIT_SSY_PLANE) != 0;
  const bool f1 = (c.sidMode & SE_SIDBIT_ECL_T0) != 0;
  if (!f1 && !f2) return sp;                       // plane 0 is Swiss's
  sp.fActive = true;
  sp.p.plane = f2 ? 2 : 1;
  const int32 mode = c.sidMode & 0xFF;
  if (mode == SE_SIDM_USER) {
    sp.p.t0Et = c.sidT0;
    sp.p.A0 = c.sidAyanT0;
  } else if (mode >= 0 && mode < SE_NSIDM_PREDEF) {
    char serrA[AS_MAXCH];
    double t0 = ayanamsa[mode].t0, daya = 0.0;
    // t0 == 0 is Swiss's way of saying THIS MODE HAS NO ANCHOR EPOCH: its
    // zero point is defined by where something IS -- a star, the galactic
    // centre, the galactic node -- evaluated at the instant asked. Twelve of
    // the 47 are like that. 3.5a's sentence presupposes an A0 and a t0, so
    // there is nothing here to carry onto a fixed plane and the object is
    // refused.
    //
    // THIS USED TO BE AN ACCIDENT AND NOT A CHECK. Without it the code went
    // on to ask for the ayanamsa at JD 0 -- 4713 BCE -- which fails only
    // because reaching that far back needs seplm48.se1. With the file absent
    // the object was refused and the refusal looked principled; with it
    // present the anchor was built at 4713 BCE and a plane-2 chart came back
    // with errCode 0 and a wrong number. The maintainer's own ephemeris mount
    // has that file, so the defect was live on the machine this was written
    // on and invisible on the bundled ephemeris the gates run against. A
    // refusal that depends on which files are installed is not a refusal.
    if (t0 == 0.0) {
      // No anchor epoch. 3.5a: plane 1 IS the ecliptic of the anchor epoch, so
      // there is nothing for such a zodiac to have; plane 2 takes the zero
      // point from the instant asked instead, which for these is the
      // definition rather than an approximation of one -- their zero point
      // genuinely is defined on the ecliptic of the instant. The same
      // construction is an approximation for an epoch-anchored zodiac (30.75"
      // out for Lahiri) and exact here, which is the distinction the clause
      // turns on.
      if (sp.p.plane != 2) {
        sp.err = eph::kOErrUnsupported;
        return sp;
      }
      sp.fPerRow = true;
      return sp;
    }
    swe_set_sid_mode_r(ctx, mode, 0, 0);           // the plain mode, no bits
    if (ayanamsa[mode].t0_is_UT)
      t0 += swe_deltat_ex_r(ctx, t0, SEFLG_SWIEPH, serrA);
    // The TABLE's ayan_t0 is the published value; Swiss corrects it for the
    // precession model the ayanamsa was defined under, so the corrected value
    // is what swe_get_ayanamsa_ex answers AT t0 -- up to 17" apart, which is
    // not a rounding difference.
    //
    // SEFLG_NONUT IS LOAD-BEARING. Without it this is the TRUE ayanamsa at
    // t0, the arc from the TRUE equinox; 3.5a wants the zero point's longitude
    // on the MEAN ecliptic and equinox of t0, which is that value less the
    // nutation in longitude at t0. Putting the true value on a mean frame
    // moves the origin by dpsi(t0) -- -3.311" for Fagan/Bradley, +16.777" for
    // Lahiri, +17.346" for Raman, so it is zodiac-dependent and looks exactly
    // like the defect this code exists to fix. The other engine's anchors are
    // held as mean values for the same reason, and their cross-test caught
    // this within an hour of the first version landing: our plane 1, which had
    // agreed with theirs to 0.003" while Swiss computed it, regressed by
    // precisely dpsi(t0).
    if (swe_get_ayanamsa_ex_r(ctx, t0, SEFLG_SWIEPH | SEFLG_NONUT,
        &daya, serrA) < 0) {
      // A zodiac whose zero point cannot be constructed is REFUSED, not
      // answered on some other plane. A.8 is a request field.
      sp.err = eph::kOErrUnsupported;
      return sp;
    }
    sp.p.t0Et = t0;
    sp.p.A0 = daya;
  } else {
    sp.err = eph::kOErrUnsupported;
    return sp;
  }
  if (sp.p.plane == 2)
    sp.p.lonOrigin = SidPlaneOrigin(ctx, sp.p.t0Et, sp.p.A0);
  return sp;
}

// A node's frame, per 3.5a as the 2026-09-18 drop amended it: a node lies on
// the mean ecliptic of DATE, and the profile's frame gives the coordinates it
// is expressed in rather than which point it is.
//
// Swiss handed a fixed frame answers a THIRD point, which is what this server
// shipped until now. The of-date node's longitude comes back precessed and
// correct, and the latitude is neither the rotation's nor zero: +10.013" at
// 1800 where rotating the of-date node gives +61.358", -0.884" at 1900 against
// -46.882", and at 2100 not even the same sign. Found by Ephemeris
// Prometheia's cross-test; EPHEMERIS_ACCURACY_REGISTRY.md 4.2.
//
// So the node is computed on the mean ecliptic of date and rotated here.
// swi_precess is the same precession swe_calc gives the BODIES, which is the
// property that matters: a J2000 chart has to be internally consistent, and a
// model reimplemented here would disagree with our own planets before it
// disagreed with anyone else's. tools/frame-rotation-fit.py is the net --
// it derives the rotation from the bodies alone and requires the node to
// follow it.
//
// Frames 0 and 1 are NOT touched, and that is measured rather than assumed:
// nutation rotates the equator, not the ecliptic, so a point on the mean
// ecliptic of date lies on the true ecliptic of date too. Asking Swiss for
// both and comparing gives the same numbers to every digit.
static void RotateNodeToFixedFrame(swe_ctx *ctx, double jdEt, int32 iflag,
                                   double *xx) {
  double v[6], pol[6];
  const bool fRect = (iflag & SEFLG_XYZ) != 0;
  const bool fEqu = (iflag & SEFLG_EQUATORIAL) != 0;
  // swi_polcart_sp() and swi_cartpol_sp() work in RADIANS; swe_nod_aps()
  // answers in degrees unless the caller asked otherwise. Feeding degrees
  // to them silently produces a direction somewhere else entirely -- the
  // first build of this put the 1800 node 34 degrees from where it belongs.
  const double toRad = (iflag & SEFLG_RADIANS) ? 1.0 : DEGTORAD;
  double eps;

  if (fRect) {
    memcpy(v, xx, sizeof(v));
  } else {
    memcpy(pol, xx, sizeof(pol));
    pol[0] *= toRad; pol[1] *= toRad; pol[3] *= toRad; pol[4] *= toRad;
    swi_polcart_sp(pol, v);
  }
  // Precession is defined on the equator, so ecliptic input goes there and
  // back. The obliquity out is J2000's, not the instant's.
  if (!fEqu) {
    eps = swi_epsiln(ctx, jdEt, 0);
    swi_coortrf(v, v, -eps);
    swi_coortrf(v + 3, v + 3, -eps);
  }
  swi_precess(ctx, v, jdEt, 0, J_TO_J2000);
  swi_precess_speed(ctx, v, jdEt, 0, J_TO_J2000);
  if (iflag & SEFLG_ICRS) swi_bias(ctx, v, jdEt, iflag, FALSE);
  if (!fEqu) {
    eps = swi_epsiln(ctx, J2000, 0);
    swi_coortrf(v, v, eps);
    swi_coortrf(v + 3, v + 3, eps);
  }
  if (fRect) {
    memcpy(xx, v, sizeof(v));
  } else {
    swi_cartpol_sp(v, pol);
    pol[0] /= toRad; pol[1] /= toRad; pol[3] /= toRad; pol[4] /= toRad;
    memcpy(xx, pol, sizeof(pol));
  }
}

// Rows [row0, row0 + rows) of one prepared object into the entry (3.5
// columns): the base six, then the extra columns present in bit order --
// ayanamsa (the profile's, 0 tropical) and the delta T used, in seconds.
//
// Called once per block of rows, so the per-object Swiss settings are applied
// again on every entry: the context is the loop's, and another object -- or
// another request's -- block ran on it in between.
static void ComputeObjectRows(swe_ctx *ctx, const eph::Request &req, uint32_t iObj,
                              eph::CacheEntry *e, ObjPrep *prep, uint32_t row0,
                              uint32_t rows) {
  if (prep->fDead) return;
  const eph::Profile &pf = req.profiles[req.objs[iObj].profile];
  eph::Meta &m = e->meta[iObj];
  const uint32_t nTime = e->nTimeRows, nCols = e->nCols;
  double *dst0 = e->cols.data() + (size_t)iObj * nTime * nCols;
  const eph::swiss::SwissCall &c = prep->c;
  // 3.5a's fixed planes are this server's own arithmetic; see PrepareSidPlane.
  // The body is then asked TROPICALLY, in the ecliptic of J2000, and the
  // zodiac applied afterwards -- so Swiss never sees the plane bits and it no
  // longer matters that it declines them for 16 of the 47 ayanamsas.
  const SidPlaneReq sid = PrepareSidPlane(ctx, c);
  if (sid.err != 0) {
    m.errCode = sid.err;
    m.firstFailedRow = 0;
    for (uint32_t r = row0; r < row0 + rows && r < nTime; r++) {
      double *d = dst0 + (size_t)r * nCols;
      for (uint32_t k = 0; k < nCols; k++) d[k] = NAN;
    }
    return;
  }
  const int32 iflagBody = sid.fActive
    ? ((c.iflag & ~(int32)(SEFLG_SIDEREAL | SEFLG_ICRS)) | SEFLG_J2000 | SEFLG_NONUT)
    : c.iflag;
  if (c.fSidereal) {
    // The mode has to be live on the context even when the fixed-plane path
    // never hands SEFLG_SIDEREAL to Swiss, because it still ASKS Swiss for the
    // ayanamsa -- for the plane-0 true-anchor correction and, on the
    // instant-defined clause, for every row's zero point. Without this the
    // query answers from whatever mode ran last, and the tell is unmistakable:
    // four different zodiacs returned the IDENTICAL plane-2 longitude.
    // The plane bits are stripped for the fixed planes, which compute the
    // plane themselves; plane 0 keeps the mode whole, and has no bits anyway.
    swe_set_sid_mode_r(ctx, sid.fActive ? (c.sidMode & 0xFF) : c.sidMode,
      c.sidT0, c.sidAyanT0);
  }
  if (c.fTopo) swe_set_topo_r(ctx, c.topo[0], c.topo[1], c.topo[2]);
  const bool fRect = pf.form == eph::kFormRectangular;
  char serr[AS_MAXCH], star[SE_MAX_STNAME * 2];
  bool &fNamed = prep->fNamed;
  for (uint32_t r = row0; r < row0 + rows && r < nTime; r++) {
    eph::Time t = req.RowTime(r);
    // 3.5: an engine that takes one double evaluates jd1 + jd2, and RowTime
    // put the row's offset into jd2 first.
    double jd = t.jd1 + t.jd2;
    // 3.5: the row's delta T -- the request's table, else its one value,
    // else NaN, which is this server's own model (swe_deltat_ex_r).
    const double dtRow = req.DeltaTAt(r);
    const bool fDtGiven = std::isfinite(dtRow);
    // The TT instant, for the entry points that have no UT form here.
    auto jdEt = [&]() -> double {
      if (!c.fUT && !c.fAddDeltaT) return jd;   // the instant is TT already
      if (fDtGiven) return jd + dtRow / 86400.0;
      return jd + swe_deltat_ex_r(ctx, jd, SEFLG_SWIEPH, nullptr);
    };
    // 3.5 says the request's delta T governs "UT1<->TT AND EARTH
    // ROTATION". Converting the instant above satisfies the first half
    // only. The second half is inside Swiss: a topocentric observer's
    // place comes from sidereal time, which Swiss derives from UT = TT
    // minus ITS OWN delta T, whatever the client sent. So a TT request
    // with an explicit delta T moved nothing at all -- byte-identical
    // output for deltaTSec 0 and 100 -- while the observer sat where
    // Swiss's model put them.
    //
    // Found by the Prometheia cross-test, reported as "the delta T a
    // request sends is ignored". It is not ignored: a UT1 request moves
    // the Moon 75.46 arcsec between those two values, through the
    // conversion above. What was ignored is the Earth-rotation half, and
    // a TT request is where that is the ONLY half, which is why their
    // probe saw nothing move.
    //
    // swe_set_delta_t_userdef_r puts the client's value inside Swiss for
    // every conversion it makes internally, which is what the sentence
    // asks for. Reset to SE_DELTAT_AUTOMATIC afterwards, or one
    // request's delta T would leak into the next on this loop's engine.
    if (fDtGiven)
      swe_set_delta_t_userdef_r(ctx, dtRow / 86400.0);
    double xx[6];
    int32_t ret = -1;
    serr[0] = '\0';
    switch (c.kind) {
      case eph::swiss::kCallCalc:
        ret = c.fUT && !fDtGiven ? swe_calc_ut_r(ctx, jd, c.ipl, iflagBody, xx, serr)
                                 : swe_calc_r(ctx, jdEt(), c.ipl, iflagBody, xx, serr);
        break;
      case eph::swiss::kCallPctr:
        ret = swe_calc_pctr_r(ctx, jdEt(), c.ipl, c.iplCenter, iflagBody, xx, serr);
        break;
      case eph::swiss::kCallNodAps: {
        double xn[6], xd[6], xp[6], xa[6];
        // The point is the one on the mean ecliptic of date; the frame only
        // says what to express it in. See RotateNodeToFixedFrame() above.
        // Keyed on iflagBody, not on the request's own flags: a sidereal
        // FIXED PLANE asks the body in the J2000 ecliptic too (see
        // PrepareSidPlane), so the node needs the same of-date-then-rotate
        // treatment there. Keying this on c.iflag, and switching it off
        // whenever the sidereal path was active, put Swiss's third point
        // into every fixed-plane chart that carried a node -- caught by the
        // suite leg asserting that the server/local divergence on plane 2
        // is an ORIGIN SHIFT and nothing else: a node moved in latitude,
        // which an origin shift cannot do.
        const bool fFixedFrame = (iflagBody & SEFLG_J2000) != 0;
        const int32 iflagNode = fFixedFrame
          ? ((iflagBody & ~(int32)(SEFLG_J2000 | SEFLG_ICRS)) | SEFLG_NONUT)
          : iflagBody;
        ret = c.fUT && !fDtGiven
                ? swe_nod_aps_ut_r(ctx, jd, c.ipl, iflagNode, c.nodMethod, xn, xd, xp, xa, serr)
                : swe_nod_aps_r(ctx, jdEt(), c.ipl, iflagNode, c.nodMethod, xn, xd, xp, xa, serr);
        const double *px = c.point == 0 ? xn : c.point == 1 ? xd : c.point == 2 ? xp : xa;
        if (ret >= 0) {
          memcpy(xx, px, sizeof(xx));
          if (fFixedFrame) RotateNodeToFixedFrame(ctx, jdEt(), iflagBody, xx);
        }
        break;
      }
      case eph::swiss::kCallFixstar:
        snprintf(star, sizeof(star), "%s", c.star.c_str());
        ret = c.fUT && !fDtGiven ? swe_fixstar2_ut_r(ctx, star, jd, iflagBody, xx, serr)
                                 : swe_fixstar2_r(ctx, star, jdEt(), iflagBody, xx, serr);
        break;
      default:
        snprintf(serr, sizeof(serr), "unsupported call");
        break;
    }
    if (fDtGiven)
      swe_set_delta_t_userdef_r(ctx, SE_DELTAT_AUTOMATIC);
    double *dst = dst0 + (size_t)r * nCols;
    if (ret < 0) {
      for (uint32_t k = 0; k < nCols; k++) dst[k] = NAN;
      if (m.firstFailedRow == eph::kRowNone) {
        std::string text;
        m.firstFailedRow = r;
        m.errCode = ObjErrOf(serr, c.kind == eph::swiss::kCallFixstar, &text);
        // "The file is missing" and "this instant needs data from outside
        // the span" are the same Swiss message, and they meant the same
        // A.17 code here: 4. They are not the same thing, and the
        // difference is exactly what a client acts on -- one says try
        // another instant, the other says the server is misconfigured.
        //
        // At the first instant of the .se1 span, a light-time correction
        // has to look a few minutes EARLIER, into a century file this
        // server does not carry, and Swiss says the file is not found.
        // We answered 4 with the text "not on the server's path", which
        // was untrue: the file for the instant asked about is right
        // there. Found by the Prometheia project's cross-test, leg 2.
        //
        // So ask the cheap question directly, and only on the error
        // path: does this BODY compute at an instant we know is covered?
        // If it does, the body's data is on the path and what failed is
        // about the INSTANT -- coverage (3). If it fails there too, the
        // body's file really is absent -- data missing (4).
        //
        // One probe settles three cases that used to be one: the first
        // instant of the span with a light-time correction (the body
        // computes at J2000, so 3); an instant far outside any century
        // file (likewise 3, which the narrower first version of this fix
        // still got wrong); and an asteroid whose .se1 was never shipped
        // (fails at J2000 too, so 4).
        //
        // TRUEPOS on the probe so the probe itself cannot fail for the
        // reach reason it is trying to distinguish. J2000 is the
        // reference because every ephemeris this server is meant to
        // carry spans it; a bundle that did not would report 4 where 3
        // was meant, which is the honest limit of this test.
        if (m.errCode == eph::kOErrDataMissing &&
            (c.kind == eph::swiss::kCallCalc ||
             c.kind == eph::swiss::kCallPctr)) {
          double xxT[6];
          char serrT[AS_MAXCH];
          // The planet-centred call needs its own probe, or the nine
          // Jupiter-centred rows at the first instant of the span kept
          // answering 4 while every other observer at that instant
          // answered 3 -- which is what the cross-test's rerun found.
          int32_t retT = c.kind == eph::swiss::kCallPctr
            ? swe_calc_pctr_r(ctx, 2451545.0, c.ipl, c.iplCenter,
                              c.iflag | SEFLG_TRUEPOS, xxT, serrT)
            : swe_calc_r(ctx, 2451545.0, c.ipl, c.iflag | SEFLG_TRUEPOS, xxT,
                         serrT);
          if (retT >= 0) {
            m.errCode = eph::kOErrCoverage;
            text = "the instant is outside this ephemeris's coverage";
          }
        }
        m.errText = WireText(text.c_str());
      }
      continue;
    }
    if (c.fOpposite) {
      // The descending node of a named node body: the point opposite --
      // in DIRECTION only.
      //
      // Both nodes lie on the line where the orbital plane meets the
      // ecliptic, and that line passes through the geocentre, so the
      // descending node's direction is exactly the ascending one's plus
      // 180 degrees. Its DISTANCE is not: the two ends of that line sit
      // at different radii on the osculating ellipse, p/(1 +/- e cos w).
      // Flipping the vector kept the ascending node's radius, which at
      // J2000 answered 365,838 km where the descending node is at
      // 396,065 -- 30,227 km, eight per cent, wrong.
      //
      // Found by the Ephemeris Prometheia cross-test, which noticed our
      // two true nodes reporting the SAME distance, and is the same
      // family as the mean node's constant (477ad49). swe_nod_aps
      // computes both nodes properly, so its descending radius is taken
      // here while the direction stays the named body's -- which is
      // exact, and which nod_aps's own ascending node differs from by
      // 0.06 arcsec, noise between two Swiss computations of one point.
      double xnA[6], xdA[6], xpA[6], xaA[6], rDesc = 0.0, rdotDesc = 0.0;
      char serrA[AS_MAXCH];
      if (swe_nod_aps_r(ctx, jdEt(), SE_MOON, c.iflag, SE_NODBIT_OSCU,
            xnA, xdA, xpA, xaA, serrA) >= 0) {
        rDesc = xdA[2];
        rdotDesc = xdA[5];
      } else {
        // The radius could not be had -- at the first instant of the
        // .se1 span, where this call reaches earlier than the files go.
        // FAIL the row rather than keep the ascending node's radius: a
        // fallback to the number this fix exists to remove is worse than
        // an error, because it is silent and it is wrong only sometimes.
        // The osculating siblings already fail there; this one was
        // answering, with the old value, which the Prometheia cross-test
        // caught at 1800-01-01.
        for (uint32_t k = 0; k < nCols; k++) dst0[(size_t)r * nCols + k] = NAN;
        if (m.firstFailedRow == eph::kRowNone) {
          m.firstFailedRow = r;
          m.errCode = eph::kOErrCoverage;
          m.errText = WireText("the instant is outside this ephemeris's "
                               "coverage");
        }
        continue;
      }
      if (fRect) {
        double rAsc = sqrt(xx[0]*xx[0] + xx[1]*xx[1] + xx[2]*xx[2]);
        double f = (rDesc > 0.0 && rAsc > 0.0) ? rDesc / rAsc : 1.0;
        // The direction is exact, so scaling the position to the right
        // radius is exact too. The velocity is scaled with it, which is
        // right for its transverse part and leaves the radial part
        // uncorrected; a client wanting the node's range rate should ask
        // in spherical form, where it is carried exactly.
        for (int k = 0; k < 3; k++) xx[k] = -xx[k] * f;
        for (int k = 3; k < 6; k++) xx[k] = -xx[k] * f;
      } else {
        xx[0] = swe_degnorm(xx[0] + 180.0);
        xx[1] = -xx[1];
        xx[4] = -xx[4];
        if (rDesc > 0.0) {
          xx[2] = rDesc;
          xx[5] = rdotDesc;
        }
      }
    }
    if (!pf.speeds) xx[3] = xx[4] = xx[5] = 0.0;
    // 3.5a: a star whose catalogue entry has no parallax has no distance.
    // Swiss answers 1e9 AU for one (sweph.c's rdist), light time and
    // aberration moving it a little; anything past 1e8 AU is that placeholder.
    if (sid.fActive) {
      EPHSIDPLANE sp = sid.p;
      if (sid.fPerRow) {
        // The instant's own zero point: the true-anchor ayanamsa here, laid on
        // the mean ecliptic of this instant and projected onto the plane.
        char serrP[AS_MAXCH];
        double a0 = 0.0;
        if (swe_get_ayanamsa_ex_r(ctx, jdEt(),
              c.iflag | SEFLG_NOABERR | SEFLG_NOGDEFL, &a0, serrP) < 0) {
          m.errCode = eph::kOErrCoverage;
          if (m.firstFailedRow == eph::kRowNone) m.firstFailedRow = r;
          for (uint32_t k = 0; k < nCols; k++) dst[k] = NAN;
          continue;
        }
        sp.t0Et = jdEt();
        sp.A0 = a0;
        sp.lonOrigin = SidPlaneOrigin(ctx, sp.t0Et, sp.A0);
      }
      ApplySidPlane(ctx, &sp, fRect, xx);
    }
    // The anchors sentence, on plane 0: Swiss subtracted the ayanamsa built
    // from the APPARENT anchor. Ask it for both and put the difference back.
    // A scalar on longitude is the whole correction -- an ayanamsa is a
    // rotation about the ecliptic pole, so no latitude moves, which is also
    // what the cross-test measured (the other engine's rows differ from ours
    // in longitude alone, by each anchor's own aberration).
    if (sid.fTrueAnchor && !sid.fActive && !fRect) {
      char serrA[AS_MAXCH];
      double aApp = 0.0, aTrue = 0.0;
      if (swe_get_ayanamsa_ex_r(ctx, jdEt(), c.iflag, &aApp, serrA) >= 0 &&
          swe_get_ayanamsa_ex_r(ctx, jdEt(),
            c.iflag | SEFLG_NOABERR | SEFLG_NOGDEFL, &aTrue, serrA) >= 0) {
        xx[0] += aApp - aTrue;
        xx[0] = fmod(xx[0], 360.0);
        if (xx[0] < 0.0) xx[0] += 360.0;
      }
    }
    if (c.kind == eph::swiss::kCallFixstar && !fRect && xx[2] > 1e8)
      m.flags |= eph::kMetaNoDistance;
    memcpy(dst, xx, sizeof(xx));
    uint32_t k = 6;
    if (e->columnsPresent & eph::kColAyanamsa) {
      double daya = 0.0;
      if (sid.fActive) {
        daya = sid.p.A0;         // on a fixed plane the zodiac IS its anchor
        if (sid.fPerRow) {
          char serrP[AS_MAXCH];
          if (swe_get_ayanamsa_ex_r(ctx, jdEt(),
                c.iflag | SEFLG_NOABERR | SEFLG_NOGDEFL, &daya, serrP) < 0)
            daya = NAN;
        }
      } else if (c.fSidereal) {
        char serrA[AS_MAXCH];
        // Same flags the position used, so the column and the longitudes
        // describe one zodiac rather than two.
        const int32 fl = sid.fTrueAnchor
          ? (c.iflag | SEFLG_NOABERR | SEFLG_NOGDEFL) : c.iflag;
        if (swe_get_ayanamsa_ex_r(ctx, jdEt(), fl, &daya, serrA) < 0) daya = NAN;
      }
      dst[k++] = daya;
    }
    if (e->columnsPresent & eph::kColDeltaT)
      dst[k++] = fDtGiven ? dtRow : swe_deltat_ex_r(ctx, jd, SEFLG_SWIEPH, nullptr) * 86400.0;
    m.rowsOk++;
    if (!fNamed) {
      fNamed = true;
      if (c.kind == eph::swiss::kCallFixstar) {
        m.name = WireText(star);
      } else {
        char nm[AS_MAXCH] = "";
        swe_get_planet_name_r(ctx, c.ipl, nm);
        std::string s(nm);
        if (c.kind == eph::swiss::kCallNodAps || c.fOpposite) {
          static const char *const kPoint[] = {" ascending node", " descending node",
                                               " perihelion", " aphelion"};
          s += kPoint[c.fOpposite ? 1 : c.point & 3];
        }
        m.name = WireText(s.c_str());
      }
    }
  }
}

// The object-level verdict, once its last row is computed (3.5): a partially
// answered object says so, and one that answered nothing without saying why
// is this server's fault, not the question's.
static void FinishObject(eph::CacheEntry *e, uint32_t iObj) {
  eph::Meta &m = e->meta[iObj];
  if (m.rowsOk > 0 && m.firstFailedRow != eph::kRowNone) m.flags |= eph::kMetaPartial;
  if (m.rowsOk == 0 && m.errCode == eph::kOErrNone) m.errCode = eph::kOErrInternal;
}

// ---------------------------------------------------------------------------
// 4. Loop wiring
//
// One uWS App per event-loop thread; SO_REUSEPORT load-balances accepted
// connections across loops. Each loop owns one Swiss context and a private
// LRU result cache (its share of --cache-mb), so nothing on the request path
// is shared between threads.
// ---------------------------------------------------------------------------

// Counters for /metrics. Each loop writes only its own, so no two threads
// ever write one; the atomics are for the scrape, which a different loop's
// thread may serve, and sums every loop's.
static const double kComputeBucketsMs[] = {1, 5, 10, 50, 100, 250, 500,
                                           1000, 2500, 5000};
enum { kComputeBuckets = sizeof(kComputeBucketsMs) / sizeof(*kComputeBucketsMs) };
struct Metrics {
  std::atomic<uint64_t> connOpen{0}, connTotal{0}, hellos{0};
  std::atomic<uint64_t> requests{0}, cells{0}, cacheHits{0}, cacheMisses{0};
  std::atomic<uint64_t> lookups{0}, cancels{0};
  std::atomic<uint64_t> errors[kErrMax + 1] = {};  // by code; 0 other
  std::atomic<uint64_t> refusedConns{0};       // at the upgrade, over a cap
  std::atomic<uint64_t> helloTimeouts{0};
  std::atomic<uint64_t> bytesSent{0}, backpressureWaits{0};
  std::atomic<uint64_t> computeBucket[kComputeBuckets] = {};
  std::atomic<uint64_t> computeCount{0}, computeMicros{0};
};

struct LoopCtx {
  int index = 0;
  Metrics m;
  // Operations (section 7): the listen socket, so a drain can close it,
  // and every open WebSocket on this loop, so a drain can end them. Both
  // touched only on this loop's thread.
  us_listen_socket_t *listenSock = nullptr;
  us_timer_t *helloTimer = nullptr;         // the HELLO deadline's sweep
  // The block-wise computation's timer (3.4 CANCEL): ticks every millisecond
  // while any connection on this loop has an answer still being computed,
  // once a second otherwise. workCursor rotates which connection is served
  // first, so one long request cannot hold a tick against the others.
  us_timer_t *workTimer = nullptr;
  bool fWorkArmed = false;
  size_t workCursor = 0;
  std::unordered_set<void *> socks;
  uWS::Loop *loop = nullptr;
  std::unique_ptr<uWS::App> app;        // plain ws://, or
  std::unique_ptr<uWS::SSLApp> sslApp;  // wss:// under --tls-cert
  std::vector<swe_ctx *> pool;   // private slice, never shared
  size_t nextCtx = 0;
  // The per-loop LRU result cache (eph_cache.h), keyed on datasetId and the
  // question block, capped at this loop's share of --cache-mb.
  eph::ResultCache cache;

  swe_ctx *TakeContext() {
    if (pool.empty()) return nullptr;
    swe_ctx *ctx = pool[nextCtx % pool.size()];
    nextCtx++;
    return ctx;
  }
};

static thread_local LoopCtx *tlc = nullptr;   // this thread's loop state
static std::atomic<uint64_t> gConnIds{0};     // the log's conn= ids
static std::atomic<bool> gDraining{false};

// ---------------------------------------------------------------------------
// Limits (production plan Phase 3). Shared by every loop, so behind one
// mutex; taken once per connection and once per REQUEST, which is nothing
// beside a REQUEST's computation.
// ---------------------------------------------------------------------------
static std::mutex gLimitsMu;
static uint32_t gConnsTotal = 0;
static std::unordered_map<std::string, uint32_t> gConnsByAddr;
struct CellBucket {
  double cells;
  std::chrono::steady_clock::time_point t;
};
static std::unordered_map<std::string, CellBucket> gBuckets;
static std::unordered_set<std::string> gTokens;

// Take a place for a new connection, or say why not.
static const char *AdmitConnection(const std::string &addr) {
  std::lock_guard<std::mutex> lock(gLimitsMu);
  if (gOpt.maxConns && gConnsTotal >= gOpt.maxConns)
    return "the server is at its connection limit";
  if (gOpt.maxConnsPerAddr && gConnsByAddr[addr] >= gOpt.maxConnsPerAddr)
    return "too many connections from this address";
  gConnsTotal++;
  gConnsByAddr[addr]++;
  return nullptr;
}

static void ReleaseConnection(const std::string &addr) {
  std::lock_guard<std::mutex> lock(gLimitsMu);
  if (gConnsTotal) gConnsTotal--;
  auto it = gConnsByAddr.find(addr);
  if (it != gConnsByAddr.end() && --it->second == 0)
    gConnsByAddr.erase(it);
}

Conn::~Conn() {
  if (fCountedAddr) ReleaseConnection(addr);
}

// Charge a REQUEST's cells to its budget: the bucket refills at
// --cells-per-sec up to --max-cells, so one full request is always
// possible from a full bucket. Returns 0 when charged, else the seconds
// until it would be.
static double ChargeCells(const std::string &key, uint64_t cells) {
  if (!gOpt.cellsPerSec) return 0.0;
  auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(gLimitsMu);
  auto it = gBuckets.find(key);
  if (it == gBuckets.end())
    it = gBuckets.emplace(key, CellBucket{(double)gOpt.maxCells, now}).first;
  CellBucket &b = it->second;
  double dt = std::chrono::duration<double>(now - b.t).count();
  b.cells = std::min((double)gOpt.maxCells, b.cells + dt * gOpt.cellsPerSec);
  b.t = now;
  if ((double)cells <= b.cells) {
    b.cells -= (double)cells;
    return 0.0;
  }
  // Buckets for addresses that have gone quiet refill to full and carry no
  // information; drop them now and then so the map cannot grow without end.
  if (gBuckets.size() > 100000)
    for (auto i = gBuckets.begin(); i != gBuckets.end();)
      i = std::chrono::duration<double>(now - i->second.t).count() * gOpt.cellsPerSec
            >= gOpt.maxCells ? gBuckets.erase(i) : std::next(i);
  return ((double)cells - b.cells) / gOpt.cellsPerSec;
}

static bool LoadTokens() {
  if (gOpt.tokensFile.empty()) return true;
  FILE *f = fopen(gOpt.tokensFile.c_str(), "r");
  if (!f) {
    LogEvt(kLogError, "fatal").S("file", gOpt.tokensFile)
      .Msg("cannot read --tokens %s: %s", gOpt.tokensFile.c_str(), strerror(errno));
    return false;
  }
  char line[512];
  while (fgets(line, sizeof(line), f)) {
    std::string t(line);
    while (!t.empty() && (t.back() == '\n' || t.back() == '\r' ||
                          t.back() == ' ' || t.back() == '\t'))
      t.pop_back();
    size_t i = t.find_first_not_of(" \t");
    if (i == std::string::npos || t[i] == '#') continue;
    t = t.substr(i);
    if (t.size() > eph::kTokenMax) {
      LogEvt(kLogError, "fatal").S("file", gOpt.tokensFile)
        .Msg("a token in %s is longer than %zu bytes; refusing to start",
             gOpt.tokensFile.c_str(), eph::kTokenMax);
      fclose(f);
      return false;
    }
    gTokens.insert(t);
  }
  fclose(f);
  // Counted, never printed: the log is no place for credentials.
  LogEvt(kLogInfo, "tokens").U("count", gTokens.size())
    .S("file", gOpt.tokensFile).B("required", gOpt.requireToken);
  return true;
}

// ---- Block-wise computation (3.4 CANCEL) ----------------------------------
//
// A REQUEST used to be computed inside the callback that read it. That made
// CANCEL meaningless -- by the time the cancel was read the work was done and
// only bytes were left to drop -- and one large request blocked its loop for
// every other connection on it (64 objects x 20,000 rows measured 13.7 s,
// EPHEMERIS_REVIEW.md S4).
//
// Now a request is computed in blocks of rows across loop turns, driven by
// each loop's work timer. Three properties follow, and the `cancel` cap is
// advertised because of them:
//   - a CANCEL stops the computing, not just the sending;
//   - a cancelled request caches NOTHING: the entry is put in the cache only
//     when its last row is computed, so a later identical question is a miss
//     that computes a whole answer rather than a hit on half of one;
//   - the loop keeps answering everything else while a big request runs.
//
// What it does NOT do is stream a block as it completes, which 3.4 mentions
// under CANCEL. DATA chunk 0 MUST carry every object's META, clients use
// chunk 0's, and META counts the rows that computed and names the first that
// failed -- facts about the WHOLE answer. Sending chunk 0 before the last row
// is computed means writing them before they are known. See the work log.

// Rows of ONE object between two checks of the clock. A block is a range of
// rows of a single object, in request order, exactly as the whole answer used
// to be computed in one go -- and that ORDER is worth what it costs to keep:
// computing every object at one instant before the next instant instead,
// which is what 3.9 suggests memoising the per-instant work would reward,
// measured 46% SLOWER on this engine (a 30-body 1000-row cold window: 463 ms
// against 317 ms). Swiss's own caches are per body, and asking one body for
// consecutive instants is what they are built for. The work log has the
// numbers.
static const uint32_t kBlockRows = 64;
// How long one stream may compute in one turn of its loop, and how long the
// whole tick may take before the loop is given back.
static const double kSliceMs = 4.0, kTickMs = 12.0;
// What the work timer ticks at with nothing to compute.
static const int kWorkIdleMs = 1000;

// Start computing a REQUEST: a cache hit is the whole answer at once, a miss
// leaves a Work for the loop's timer to advance. False is a whole-request
// failure (ERROR 4) with errText.
static bool BeginRequest(LoopCtx *lc, const eph::Request &req, const std::string &key,
                         Stream *stream, std::string *errText) {
  if (auto hit = lc->cache.get(key)) {
    stream->result = hit;
    stream->fHit = true;
    return true;
  }
  // Refused here rather than discovered by the work timer: a loop with no
  // Swiss context can answer nothing, and a Work it can never advance would
  // sit in the queue for ever.
  if (lc->pool.empty()) {
    *errText = "no swe context available";
    return false;
  }
  const uint32_t nObj = (uint32_t)req.objs.size(), nTime = req.nTime;
  auto entry = std::make_shared<eph::CacheEntry>();
  entry->nObj = nObj;
  entry->nTimeRows = nTime;
  // 3.5: the extra columns are the requested ones intersected with what
  // WELCOME advertised. Columns are asked per profile and DATA carries one
  // set, so the answer carries every column any profile asked for.
  uint32_t cols = 0;
  for (const eph::Profile &pf : req.profiles) cols |= pf.columns;
  entry->columnsPresent = cols & kColumnsServed;
  entry->nCols = 6 + (uint32_t)eph::PopCount(entry->columnsPresent);
  try {
    entry->cols.assign((size_t)nObj * nTime * entry->nCols, 0.0);
  } catch (const std::bad_alloc &) {
    *errText = "out of memory computing this request";
    return false;
  }
  entry->sources.push_back(gSource);
  entry->meta.assign(nObj, eph::Meta());
  stream->work.reset(new Work());
  Work *w = stream->work.get();
  w->req = req;
  w->key = key;
  w->entry = std::move(entry);
  w->prep.resize(nObj);
  return true;
}

// One turn's worth of one request. Returns true when the answer is complete,
// and leaves stream->result and the cache entry set in that case.
static bool AdvanceWork(LoopCtx *lc, Stream *stream) {
  Work *w = stream->work.get();
  swe_ctx *ctx = lc->TakeContext();
  if (ctx == nullptr) return false;   // no context: try again next turn
  auto t0 = std::chrono::steady_clock::now();
  const uint32_t nObj = (uint32_t)w->req.objs.size(), nTime = w->req.nTime;
  // Every object is prepared before any row, because chunk 0's META names
  // what each object resolved to.
  if (!w->fPrepared) {
    for (uint32_t o = 0; o < nObj; o++)
      PrepareObject(ctx, w->req, o, w->entry.get(), &w->prep[o]);
    w->fPrepared = true;
  }
  while (w->iObj < nObj) {
    uint32_t rows = kBlockRows;
    if (rows > nTime - w->rowNext) rows = nTime - w->rowNext;
    ComputeObjectRows(ctx, w->req, w->iObj, w->entry.get(), &w->prep[w->iObj], w->rowNext, rows);
    w->rowNext += rows;
    if (w->rowNext == nTime) {
      FinishObject(w->entry.get(), w->iObj);
      w->iObj++;
      w->rowNext = 0;
    }
    if (std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count() >= kSliceMs)
      break;
  }
  w->computeMs += std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - t0).count();
  if (w->iObj < nObj) return false;
  // Per-object failures are cached with the rest: the answer is a pure
  // function of the files on the configured path, and datasetId names them.
  stream->computeMs = w->computeMs;
  stream->result = w->entry;
  lc->cache.put(w->key, std::move(w->entry));
  lc->m.computeCount++;
  lc->m.computeMicros += (uint64_t)(w->computeMs * 1000.0);
  for (int b = 0; b < kComputeBuckets; b++)
    if (w->computeMs <= kComputeBucketsMs[b]) lc->m.computeBucket[b]++;
  stream->work.reset();
  return true;
}

// What a REQUEST asked, as log keys, for --log-contents: the instants, the
// profiles and every object -- a body by its NAIF id, an orbit point as
// o:naif/point/method, a star s:name, a hypothetical h:token, a designation
// d:text, elements e:name.
static std::string RequestContents(const eph::Request &req) {
  LogEvt e;
  if (req.timeMode == eph::kTimeGrid)
    e.F("jd", req.start.jd1 + req.start.jd2, 6).I("step_ns", req.stepNs);
  else
    e.F("jd", req.instants.empty() ? 0.0 : req.instants[0].Sum(), 6);
  // Keys the request's own line does not already carry: it says how many
  // profiles, which time mode and which scale; this says what was IN them.
  if (!eph::IsCanonicalNaN(req.deltaTSec)) e.F("delta_t", req.deltaTSec, 3);
  if (!req.deltaTTable.empty()) e.U("delta_t_table", req.deltaTTable.size());
  std::string profs;
  static const char *const kObs[] = {"geo", "topo", "helio", "bary", "body"};
  for (const eph::Profile &pf : req.profiles) {
    char sz[256];
    snprintf(sz, sizeof(sz), "%s%s/%d/%d/%d/c%d/s%d", profs.empty() ? "" : ",",
             kObs[pf.observer <= eph::kObsBody ? pf.observer : 0], pf.plane, pf.form,
             pf.frame, pf.corrections, pf.speeds);
    profs += sz;
    if (pf.observer == eph::kObsBody) profs += ":" + std::to_string(pf.observerBody);
    if (pf.observer == eph::kObsTopo) {
      snprintf(sz, sizeof(sz), ":%.6f:%.6f:%.1f", pf.siteLonEastDeg, pf.siteLatDeg, pf.siteHeightM);
      profs += sz;
    }
    if (!pf.zodiac.empty()) profs += "/z:" + pf.zodiac + ":" + std::to_string(pf.siderealPlane);
  }
  e.S("profile_spec", profs);
  std::string objs;
  for (const eph::Object &o : req.objs) {
    if (!objs.empty()) objs += ',';
    switch (o.kind) {
      case eph::kObjBody: objs += std::to_string(o.naif); break;
      case eph::kObjOrbitPoint:
        objs += "o:" + std::to_string(o.naif) + "/" + std::to_string(o.point) + "/" +
                std::to_string(o.method);
        break;
      case eph::kObjStar: objs += "s:" + o.name; break;
      case eph::kObjHypothetical: objs += "h:" + o.name; break;
      case eph::kObjDesignation: objs += "d:" + o.name; break;
      default: objs += "e:" + o.name; break;
    }
    if (o.profile) objs += "@" + std::to_string(o.profile);
  }
  e.S("bodies", objs);
  return e.Take();
}

// Heartbeats are uWS's own: sendPingsAutomatically sends WebSocket protocol
// pings on the idle timeout's cadence and closes silent connections;
// QWebSocket answers protocol pings by itself. The app-level PING/PONG
// messages are answered for clients without automatic pong.
static const int kIdleTimeoutSeconds = 30;

template <bool SSL>
static typename WebSocket<SSL, true, Conn>::SendStatus
SendEnvelope(WebSocket<SSL, true, Conn> *ws, uint16_t type,
             uint32_t requestId, const void *payload,
             size_t payloadLen, uint8_t version = 0) {
  std::vector<uint8_t> msg;
  msg.reserve(eph::kEnvelopeSize + payloadLen);
  // Every message in the session's version once HELLO has fixed one; an
  // older client's refusal in that client's own (3.3).
  if (version == 0) version = ((Conn *)ws->getUserData())->proto;
  eph::WriteEnvelope(&msg, type, requestId, payloadLen, version ? version : eph::kProtoVersion);
  if (payloadLen) msg.insert(msg.end(), (const uint8_t *)payload, (const uint8_t *)payload + payloadLen);
  auto st = ws->send(std::string_view((char *)msg.data(), msg.size()),
                     uWS::OpCode::BINARY);
  if (st != WebSocket<SSL, true, Conn>::SendStatus::DROPPED) {
    if (tlc) tlc->m.bytesSent += msg.size();
    ((Conn *)ws->getUserData())->bytesOut += msg.size();
  }
  return st;
}

// The protocol's ERROR codes (A.19) by name, for the log's name= key.
static const char *ErrName(int code) {
  static const char *const kNames[kErrMax + 1] = {
    "other", "malformed", "limits", "unknown_type", "internal", "source",
    "rate_limited", "token", "version", "busy", "cancelled", "unsupported", "draining"};
  return code > 0 && code <= kErrMax ? kNames[code] : "other";
}

// One ERROR (3.4). closing: the connection ends after it (1008, or 1001 when
// draining). A text never quotes a request's contents (3.8); every one here
// is fixed or a count. The server's own failures are errors in the log; a
// client's mistakes, limits and refusals are warnings about that client.
template <bool SSL>
static void SendError(WebSocket<SSL, true, Conn> *ws, uint32_t requestId, uint16_t code,
                      const char *text, uint16_t flags = 0, uint32_t retryAfterMs = 0) {
  eph::Error err;
  err.code = code;
  err.flags = flags;
  err.retryAfterMs = retryAfterMs;
  err.text = WireText(text ? text : "");
  std::vector<uint8_t> pay;
  eph::EncodeError(&pay, err);
  if (tlc) tlc->m.errors[code <= kErrMax ? code : 0]++;
  Conn *c = (Conn *)ws->getUserData();
  c->errors++;
  LogEvt(code == eph::kErrInternal ? kLogError : kLogWarn, "error")
    .Conn(c).U("req", requestId).I("code", code).S("name", ErrName(code))
    .B("closing", (flags & eph::kErrFlagClosing) != 0)
    .S("msg", std::string_view(err.text).substr(0, 160));
  SendEnvelope(ws, eph::kMsgError, requestId, pay.data(), pay.size());
  if (flags & eph::kErrFlagClosing)
    ws->end(1008, std::string_view(ErrName(code)));
}

// 3.3 step 5: an older client's refusal, in its own version and layout.
template <bool SSL>
static void SendLegacyVersionError(WebSocket<SSL, true, Conn> *ws, uint8_t version) {
  Conn *c = (Conn *)ws->getUserData();
  eph::LegacyError le;
  char sz[160];
  snprintf(sz, sizeof(sz), "this client speaks protocol %u; this server needs %u to %u "
           "-- update Astrolog", (unsigned)version, (unsigned)eph::kProtoMin,
           (unsigned)eph::kProtoVersion);
  le.requestId = 0;
  le.code = eph::kErrVersion;
  le.text = sz;
  std::vector<uint8_t> pay;
  eph::EncodeLegacyError(&pay, le);
  if (tlc) tlc->m.errors[eph::kErrVersion]++;
  c->errors++;
  LogEvt(kLogWarn, "hello.refuse").Conn(c).S("reason", "version")
    .U("client_proto", version);
  SendEnvelope(ws, eph::kMsgError, 0, pay.data(), pay.size(), version);
  ws->end(1008, "protocol too old");
}

// One line per REQUEST answered, at info: its size, whether the cache had
// it, how long computing and delivering it took, and the loop's cache.
// ERRORs have their own line (SendError), so every REQUEST gets exactly
// one of the two, or a cancel line.
template <bool SSL>
static void LogRequest(WebSocket<SSL, true, Conn> *ws, const Stream &s,
                       uint64_t bytes) {
  LogEvt e(kLogInfo, "req");
  if (!e.On()) return;
  Conn *c = (Conn *)ws->getUserData();
  e.Conn(c).U("req", s.requestId).U("objs", s.nObj).U("rows", s.nTimeRows)
    .U("cells", (uint64_t)s.nObj * s.nTimeRows).U("profiles", s.nProfiles)
    .S("time", s.timeMode == eph::kTimeList ? "list" : "grid")
    .S("scale", s.timeScale == eph::kTimeUT1 ? "ut1" : "tt")
    .U("prec", s.precision == eph::kPrecF32 ? 32 : 64).U("deadline_ms", s.deadlineMs)
    .U("chunks", (s.nTimeRows + s.chunkRows - 1) / s.chunkRows)
    .S("cache", s.fHit ? "hit" : "miss").F("compute_ms", s.computeMs, 3)
    .F("total_ms", std::chrono::duration<double, std::milli>(
         std::chrono::steady_clock::now() - s.tReq).count(), 3)
    .U("bytes", bytes).U("stalls", s.stalls).U("queued", c->out.size() - 1);
  if (tlc)
    e.U("cache_entries", tlc->cache.entries())
     .F("cache_kib", (double)tlc->cache.usedBytes() / 1024.0, 1)
     .U("cache_evictions", tlc->cache.evictions());
  e.Raw(s.contents);
}

// A stream waiting for its client to read: debug, since a slow reader is
// normal, and the count is in /metrics and on the request's line.
template <bool SSL>
static void LogStall(WebSocket<SSL, true, Conn> *ws, Stream &s) {
  s.stalls++;
  LogEvt(kLogDebug, "stall").Conn((Conn *)ws->getUserData())
    .U("req", s.requestId).U("row", s.nextRow).U("rows", s.nTimeRows)
    .U("buffered", ws->getBufferedAmount());
}

// One DATA chunk's payload (3.4): the header, the source table and META on
// chunk 0, then the chunk's rows of every object, object-major, in the
// requested precision.
static void EncodeChunk(const Stream &s, uint32_t rows, std::vector<uint8_t> *out) {
  const eph::CacheEntry &e = *s.result;
  const bool fMeta = s.chunkIndex == 0;
  const bool fLast = s.nextRow + rows == s.nTimeRows;
  out->clear();
  eph::Writer w(out);
  w.u32(s.chunkIndex); w.u32(s.nextRow); w.u32(rows); w.u32(s.nTimeRows);
  w.u8(s.precision);
  w.u8((uint8_t)((fLast ? eph::kChunkLast : 0) | (s.fIgnoredExt ? eph::kChunkIgnoredExt : 0) |
                 (fMeta ? eph::kChunkMeta : 0)));
  w.u16((uint16_t)e.nObj);
  w.u32(e.columnsPresent);
  if (fMeta) {
    eph::WriteSources(w, e.sources);
    for (const eph::Meta &m : e.meta) eph::WriteMeta(w, m);
  }
  const size_t esz = s.precision == eph::kPrecF32 ? 4 : 8;
  const size_t nPer = (size_t)rows * e.nCols;
  size_t at = out->size();
  out->resize(at + (size_t)e.nObj * nPer * esz);
  uint8_t *p = out->data() + at;
  for (uint32_t o = 0; o < e.nObj; o++) {
    const double *src = e.cols.data() + ((size_t)o * e.nTimeRows + s.nextRow) * e.nCols;
    for (size_t i = 0; i < nPer; i++) {
      uint64_t u;
      if (esz == 4) {
        float f = (float)src[i];
        uint32_t u32;
        memcpy(&u32, &f, 4);
        for (int b = 0; b < 4; b++) *p++ = (uint8_t)(u32 >> (8 * b));
      } else {
        memcpy(&u, &src[i], 8);
        for (int b = 0; b < 8; b++) *p++ = (uint8_t)(u >> (8 * b));
      }
    }
  }
}

// Stream completed answers to the client, honoring uWS backpressure: stop
// at the first BACKPRESSURE and resume from the drain callback. uWS's
// BACKPRESSURE means the frame WAS taken and buffered -- only DROPPED means
// it was not -- so a chunk that returns BACKPRESSURE counts as sent. It
// used not to, and every such chunk went out twice: a Qt client counting
// rows marked the window done a chunk early and read zeros for the rest.
// Chunks are chunkRows-sized, ascending chunkIndex, contiguous rows (3.4).
// An answer still being computed sends nothing and is stepped over: the ones
// behind it in the queue may be complete (a cache hit behind a cold window),
// and 3.5 lets a server interleave the chunks of different requests.
template <bool SSL>
static void FlushStreams(WebSocket<SSL, true, Conn> *ws) {
  Conn *c = (Conn *)ws->getUserData();
  for (auto is = c->out.begin(); is != c->out.end();) {
    Stream &s = *is;
    if (s.work) { ++is; continue; }
    while (s.nextRow < s.nTimeRows) {
      if (ws->getBufferedAmount() > kStreamBackpressure) {
        if (tlc) tlc->m.backpressureWaits++;
        LogStall(ws, s);
        return;   // the rest on drain, once the peer has read some
      }
      uint32_t rows = s.nTimeRows - s.nextRow;
      if (rows > s.chunkRows) rows = s.chunkRows;
      EncodeChunk(s, rows, &s.buf);
      // The request's line goes out BEFORE its last chunk, so it is in the
      // log by the time a client could have its answer -- a gate reading
      // the log straight after a request never races it.
      if (s.nextRow + rows == s.nTimeRows && !s.fLogged) {
        s.fLogged = true;
        LogRequest(ws, s, s.bytes + eph::kEnvelopeSize + s.buf.size());
      }
      auto st = SendEnvelope(ws, eph::kMsgData, s.requestId, s.buf.data(), s.buf.size());
      if (st == WebSocket<SSL, true, Conn>::SendStatus::DROPPED)
        return;   // not sent: this chunk again on drain
      s.bytes += eph::kEnvelopeSize + s.buf.size();
      s.nextRow += rows;
      s.chunkIndex++;
      if (st == WebSocket<SSL, true, Conn>::SendStatus::BACKPRESSURE) {
        if (tlc) tlc->m.backpressureWaits++;
        if (s.nextRow < s.nTimeRows) LogStall(ws, s);
        return;   // sent and buffered: the rest on drain
      }
    }
    is = c->out.erase(is);
  }
}

// One turn of this loop's block-wise computation (3.4 CANCEL): each
// connection's FIRST unfinished answer gets a slice, and whatever completed
// is flushed. The connections are taken in a rotating order so that a long
// request cannot hold the tick's budget against the others.
//
// The timer runs every millisecond while there is work and drops back to once
// a second when there is none: a thousand wakeups a second on an idle server
// is not free, and setting the interval to zero to stop it is only a stop on
// the epoll backend -- on kqueue a zero timer fires at once, forever.
template <bool SSL>
static void WorkTick(us_timer_t *t) {
  LoopCtx *lc = *(LoopCtx **)us_timer_ext(t);
  std::vector<void *> socks(lc->socks.begin(), lc->socks.end());
  auto t0 = std::chrono::steady_clock::now();
  bool fMore = false;
  for (size_t i = 0; i < socks.size(); i++) {
    auto *ws = (WebSocket<SSL, true, Conn> *)socks[(lc->workCursor + i) % socks.size()];
    Conn *c = (Conn *)ws->getUserData();
    Stream *s = nullptr;
    for (Stream &e : c->out)
      if (e.work) { s = &e; break; }
    if (s == nullptr) {
      // No computation left -- but there may be COMPUTED rows still unsent,
      // and this is where they used to be stranded for good. FlushStreams
      // stops at the first backpressure and resumes from uWS's drain
      // callback; drain fires when a socket that was FULL becomes writable,
      // and it does not fire when the buffer emptied without one. A large
      // answer therefore stopped mid-stream with both socket queues empty,
      // the client blocked reading, and the server idle in ep_poll holding
      // rows it would never send again. Measured: 64 objects x 8000 rows
      // hung permanently (40 minutes before it was killed), 6000 rows did
      // not, and the server logged the last stall at row 5500 with 564 KB
      // buffered -- far under the 4 MB ceiling, so it was uWS's
      // backpressure, not ours. This is the second flush path that makes
      // the drain callback an optimisation rather than the only way out.
      if (!c->out.empty()) {
        FlushStreams(ws);
        if (!c->out.empty()) fMore = true;   // keep the fast tick
      }
      continue;
    }
    if (std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count() >= kTickMs) {
      // Out of budget: the rest of the connections next turn, starting here.
      lc->workCursor = (lc->workCursor + i) % socks.size();
      return;
    }
    AdvanceWork(lc, s);
    fMore = fMore || s->work != nullptr;
    FlushStreams(ws);
  }
  if (!socks.empty()) lc->workCursor = (lc->workCursor + 1) % socks.size();
  // Another connection's work may have arrived while this ran; the arming
  // side sets the timer again, so disarm only when nothing is left.
  if (!fMore) {
    for (void *p : socks) {
      Conn *c = (Conn *)((WebSocket<SSL, true, Conn> *)p)->getUserData();
      // Unsent rows count as "more", not just uncomputed ones: dropping to
      // the idle tick with rows still queued is the hang above in slow
      // motion.
      if (!c->out.empty()) fMore = true;
    }
  }
  if (!fMore && lc->fWorkArmed) {
    lc->fWorkArmed = false;
    us_timer_set(t, SSL ? WorkTick<true> : WorkTick<false>, kWorkIdleMs, kWorkIdleMs);
  }
}

// Ask for the fast tick: a request has work to compute. Idempotent -- the
// timer keeps its fast interval until a tick finds nothing left to do.
static void ArmWorkTimer(LoopCtx *lc) {
  if (lc->workTimer == nullptr || lc->fWorkArmed) return;
  lc->fWorkArmed = true;
  us_timer_set(lc->workTimer, gOpt.Tls() ? WorkTick<true> : WorkTick<false>, 1, 1);
}

// What this server serves, checked after the codec accepted a REQUEST:
// every value the codec knows but this server did not advertise is
// ERROR 11 (3.4). NULL when all of it is served.
// Segments (representation 1) are refused here, and the segments capability
// is not advertised, so nothing in this server fits anything. When a fitter
// is written, three rules of 3.4 bind it and none of them is optional:
//   - the residuals it reports are MEASURED on a check set of at least
//     4(d+1) instants that INCLUDES BOTH ENDPOINTS (tau = +-1), not only
//     interior points: a Chebyshev interpolant's error peaks at the ends and
//     its derivative's peaks there much harder. Measured on the Moon, a fit
//     declared 1.48"/day from interior points where an independent sample
//     found 3.6"/day, and 4.65"/day with the endpoints in -- a server obeying
//     the weaker reading under-reports by two to three times, in its own
//     favour;
//   - it fits fixed 32-day lattice cells aligned from J2000 that cover the
//     span asked for, not the span itself, so two clients whose spans overlap
//     share the fits. The lattice is the server's; a client must not be able
//     to name a boundary;
//   - it may round segTargetErrArcsec onto its own ladder only DOWNWARD,
//     toward a finer fit. A client asked for a number because something
//     downstream depends on it.
static const char *UnservedOf(const eph::Request &req) {
  if (req.representation != 0) return "segments are not served by this server";
  if (req.timeScale != eph::kTimeUT1 && req.timeScale != eph::kTimeTT)
    return "time scale not served: UT1 and TT only";
  // Section 2 of the per-kind drop. A profile's correction mask can no
  // longer be judged on its own: the mask is on the PROFILE and the kind is
  // on the OBJECTS referencing it, so it is checked against every
  // (observer, kind) pair that actually uses it, and a failure refuses the
  // WHOLE request -- never a per-object error, because the profile is part
  // of the question (3.4). A profile no object references is checked
  // against 0x0004 alone. This holds whatever the representation: a
  // segments request is the same request with representation = 1 and the
  // same object list.
  {
    std::vector<uint8_t> fUsed(req.profiles.size(), 0);
    for (const eph::Object &o : req.objs) {
      if (o.profile >= req.profiles.size()) continue;   // caught in parsing
      const eph::Profile &pfO = req.profiles[o.profile];
      fUsed[o.profile] = 1;
      if (!gCaps.CorrectionMaskFor(pfO.observer, o.kind, pfO.corrections))
        return "this correction mask is not honoured for this observer and "
               "object kind";
    }
    for (size_t i = 0; i < req.profiles.size(); i++)
      if (!fUsed[i] &&
        !gCaps.CorrectionMask(req.profiles[i].observer,
                              req.profiles[i].corrections))
        return "an unreferenced profile's correction mask is not honoured "
               "for its observer";
  }
  for (const eph::Profile &pf : req.profiles) {
    // Unknown BITS are refused; a mask this observer merely narrows is
    // not. WELCOME now advertises, per observer, the terms a body will
    // actually see -- at the Sun's centre and the barycentre Swiss turns
    // aberration and deflection off inside the call, so only light time
    // varies there. Enforcing that as acceptance would refuse requests
    // this server has always answered, including Astrolog's own
    // heliocentric casts, which send the full mask whenever the user has
    // not asked for true positions (ephreq.h). It also failed
    // ephsrv-golden immediately, which is how this was caught.
    //
    // So the two are deliberately not the same question. WELCOME says
    // what the server WILL DO; acceptance declines only what it cannot
    // parse. A client that asks for more than the observer can show gets
    // the answer it would have got anyway, and META's corrApplied names
    // the terms that were really live for that object.
    // Unknown BITS are refused; a mask this observer merely narrows is
    // not, and that is a deliberate asymmetry rather than laxity.
    //
    // WELCOME advertises, per observer, what a BODY will actually see --
    // at the Sun's centre and the barycentre Swiss turns aberration and
    // deflection off inside the call. But an ORBIT POINT from those same
    // observers does honour the bits (swe_nod_aps reads them before any
    // normalisation), and the capability model is keyed on the observer,
    // so no advertisement can state both. Enforcing the narrower reading
    // as acceptance therefore DESTROYS a real capability: ephsrv-golden's
    // heliocentric Mars perihelion moves when the mask does, and refusing
    // it made that leg unaskable.
    //
    // The interop hazard this was meant to answer -- our client sending a
    // mask another server would refuse -- is fixed where it belongs, in
    // the client: ClampEphSrvReqQt() now narrows every profile to a mask
    // WELCOME lists for its observer. A server being permissive about
    // what it accepts costs nothing once no client over-asks.
    if ((pf.corrections & ~(uint8_t)eph::kCorrMask) != 0)
      return "this correction mask has bits this server does not define";
    if (!pf.zodiac.empty() && !gCaps.Zodiac(pf.zodiac)) return "zodiac not served";
  }
  // Every kind in the registry gets an answer: orbital elements (kind 4),
  // which WELCOME does not advertise yet, are a per-object error 2 (3.5).
  return nullptr;
}

// Execute + enqueue one REQUEST (parsed, checked and charged).
template <bool SSL>
static void RunRequest(WebSocket<SSL, true, Conn> *ws, LoopCtx *lc, uint32_t requestId,
                       const eph::Request &req, const std::string &key, bool fIgnoredExt) {
  Conn *c = (Conn *)ws->getUserData();

  Stream s;
  s.tReq = std::chrono::steady_clock::now();
  s.requestId = requestId;
  s.precision = req.precision;
  s.fIgnoredExt = fIgnoredExt;
  s.nObj = (uint32_t)req.objs.size();
  s.nTimeRows = req.nTime;
  s.nProfiles = (uint32_t)req.profiles.size();
  s.timeMode = req.timeMode;
  s.timeScale = req.timeScale;
  s.priority = req.priority;
  s.deadlineMs = req.deadlineMs;
  // A hint, clamped to [1, maxChunkRows]; 0 means maxChunkRows (3.4).
  s.chunkRows = req.chunkRows == 0 || req.chunkRows > kMaxChunkRows ? kMaxChunkRows : req.chunkRows;

  std::string errText;
  bool fOk;
  try {
    fOk = BeginRequest(lc, req, key, &s, &errText);
  } catch (const std::bad_alloc &) {
    // An exception out of a uWS handler is std::terminate for every loop.
    fOk = false;
    errText = "out of memory computing this request";
  }
  if (!fOk) {
    SendError(ws, requestId, eph::kErrInternal, errText.c_str());
    return;
  }
  if (gOpt.logContents) s.contents = RequestContents(req);
  c->reqs++;
  c->cells += (uint64_t)s.nObj * s.nTimeRows;
  if (s.fHit) c->hits++;
  lc->m.requests++;
  if (s.fHit) {
    lc->m.cacheHits++;
  } else {
    // The compute counters are added when the last block finishes
    // (AdvanceWork): a cancelled request contributes none of them, which is
    // the truth about what was computed.
    lc->m.cacheMisses++;
    lc->m.cells += (uint64_t)s.nObj * s.nTimeRows;
  }
  // 3.9: interactive work is answered before prefetch. A priority 0 answer
  // goes in front of any queued priority 1 one that has not begun streaming
  // (chunks of one answer stay in order, so a stream already started keeps
  // its place). The queue is also the order this connection's answers are
  // COMPUTED in, so an interactive request overtakes a prefetch that is still
  // being computed -- the blocks that prefetch has already computed are kept
  // and resumed, nothing is thrown away.
  auto it = c->out.end();
  if (s.priority == 0)
    for (auto i = c->out.begin(); i != c->out.end(); ++i)
      if (i->priority != 0 && i->nextRow == 0) { it = i; break; }
  c->out.insert(it, std::move(s));
  ArmWorkTimer(lc);
  FlushStreams(ws);
}

// A REQUEST: parsed canonically, checked against what this server serves
// and the limits it advertised, charged, then answered.
template <bool SSL>
static void HandleRequest(WebSocket<SSL, true, Conn> *ws, LoopCtx *lc, uint32_t requestId,
                          const uint8_t *pl, size_t len) {
  Conn *c = (Conn *)ws->getUserData();
  eph::Request req;
  std::string why;
  bool fIgnored = false;
  eph::Outcome o = eph::ParseRequest(pl, len, &req, &why, &fIgnored);
  if (o == eph::kMalformed) {
    SendError(ws, requestId, eph::kErrMalformed, ("malformed REQUEST: " + why).c_str());
    return;
  }
  if (o == eph::kUnsupported) {
    SendError(ws, requestId, eph::kErrUnsupported, ("unsupported REQUEST: " + why).c_str());
    return;
  }
  if (const char *szWhy = UnservedOf(req)) {
    SendError(ws, requestId, eph::kErrUnsupported, szWhy);
    return;
  }
  // The pins (A.4): answered only from the dataset or catalog named.
  // 0x8001 names the ephemeris component of the identity, 0x8003 the
  // whole datasetId; both refuse with ERROR 5 (3.5).
  for (const eph::Tlv &e : req.ext) {
    std::string pin;
    if (e.tag == eph::kReqTagEphemerisPin && eph::TlvStr8(e, &pin)) {
      std::string szEphe;
      size_t p1 = gDatasetId.find('/');
      size_t p2 = p1 == std::string::npos ? std::string::npos :
        gDatasetId.find('/', p1 + 1);
      if (p1 != std::string::npos && p2 != std::string::npos)
        szEphe = gDatasetId.substr(p1 + 1, p2 - p1 - 1);
      if (szEphe.empty() || pin != szEphe) {
        SendError(ws, requestId, eph::kErrSource, "the pinned ephemeris is not this server's dataset");
        return;
      }
    }
    if (e.tag == eph::kReqTagCatalogPin) {
      SendError(ws, requestId, eph::kErrSource, "this server has no catalogs to pin");
      return;
    }
    if (e.tag == eph::kReqTagDatasetPin && eph::TlvStr8(e, &pin) && pin != gDatasetId) {
      SendError(ws, requestId, eph::kErrSource, "the pinned datasetId is not this server's");
      return;
    }
    // A precession model (0x0003) is not selectable here: the default is
    // used and the answer says the extension was ignored.
    if (e.tag == eph::kReqTagPrecession) fIgnored = true;
  }
  // The limits WELCOME advertised (3.5): ERROR 2 before anything is
  // computed. The work itself no longer blocks the loop -- it is computed in
  // blocks across loop turns -- but a limit is still what keeps one client
  // from taking the loop's whole compute budget (EPHEMERIS_REVIEW.md S4).
  uint64_t cells = (uint64_t)req.objs.size() * (uint64_t)req.nTime;
  if (req.objs.size() > kMaxObjs || req.nTime > kMaxRows || req.profiles.size() > kMaxProfiles ||
      cells > gOpt.maxCells) {
    char sz[160];
    snprintf(sz, sizeof(sz), "REQUEST asks %zu objects, %u rows, %zu profiles, %" PRIu64
             " cells; WELCOME's limits are %u, %u, %u and %u", req.objs.size(), req.nTime,
             req.profiles.size(), cells, kMaxObjs, kMaxRows, (unsigned)kMaxProfiles, gOpt.maxCells);
    SendError(ws, requestId, eph::kErrLimits, sz);
    return;
  }
  for (const Stream &st : c->out)
    if (st.requestId == requestId) {
      SendError(ws, requestId, eph::kErrMalformed, "requestId reused while its answer is outstanding");
      return;
    }
  if (gDraining) {
    SendError(ws, requestId, eph::kErrDraining, "the server is shutting down; ask another",
              eph::kErrFlagRetryable);
    return;
  }
  if (c->out.size() >= kMaxQueuedStreams) {
    SendError(ws, requestId, eph::kErrBusy, "too many answers computed and not yet read on "
              "this connection", eph::kErrFlagRetryable);
    return;
  }
  if (!gHaveEphemeris) {
    SendError(ws, requestId, eph::kErrSource, "no ephemeris directory found at startup; start "
              "the server with --ephe");
    return;
  }
  // The compute budget (production plan Phase 3): charged whether the
  // answer is cached or not, since which it will be is not known yet.
  double wait = ChargeCells(c->budget, cells);
  if (wait > 0.0) {
    char sz[160];
    // Rounded UP to the tenth: "0.0 s" for a 12 ms wait read as "now".
    snprintf(sz, sizeof(sz), "rate limited: %u cells a second; ask again in %.1f s",
             gOpt.cellsPerSec, std::ceil(wait * 10.0) / 10.0);
    SendError(ws, requestId, eph::kErrRateLimited, sz, eph::kErrFlagRetryable,
              (uint32_t)std::ceil(wait * 1000.0));
    return;
  }
  RunRequest(ws, lc, requestId, req, eph::CacheKey(gDatasetId, pl, len, req.questionOffset),
             fIgnored);
}

// LOOKUP (3.4): answered at once from the catalogue and Swiss's own names.
template <bool SSL>
static void HandleLookup(WebSocket<SSL, true, Conn> *ws, LoopCtx *lc, uint32_t requestId,
                         const uint8_t *pl, size_t len) {
  Conn *c = (Conn *)ws->getUserData();
  eph::Lookup l;
  std::string why;
  eph::Outcome o = eph::ParseLookup(pl, len, &l, &why);
  if (o != eph::kOk) {
    SendError(ws, requestId, o == eph::kMalformed ? eph::kErrMalformed : eph::kErrUnsupported,
              ("LOOKUP: " + why).c_str());
    return;
  }
  if (l.maxMatches > kLookupMax) {
    SendError(ws, requestId, eph::kErrLimits, "LOOKUP asks more matches than WELCOME's limit");
    return;
  }
  swe_ctx *ctx = lc->TakeContext();
  if (ctx == nullptr) {
    SendError(ws, requestId, eph::kErrInternal, "no swe context available");
    return;
  }
  // 3.4: maxMatches is the budget for the WHOLE answer, filled in query
  // order, and `truncated` says the budget ran out -- not that one query was
  // cut. A query that spends nothing still keeps its (empty) place, so the
  // lists line up with the questions.
  eph::LookupResult lr;
  bool fMore = false;
  size_t budget = l.maxMatches, cMatches = 0;
  lr.sources.push_back(gSource);
  for (const std::string &query : l.queries) {
    std::vector<eph::Match> matches;
    bool fMoreHere = false;
    if (budget) Resolve(ctx, query, l.flags, budget, &matches, &fMoreHere);
    else fMore = true;
    fMore = fMore || fMoreHere;
    budget -= matches.size();
    cMatches += matches.size();
    lr.queries.push_back(std::move(matches));
  }
  lr.flags = fMore ? 1 : 0;
  std::vector<uint8_t> pay;
  eph::EncodeLookupResult(&pay, lr);
  c->lookups++;
  lc->m.lookups++;
  LogEvt e(kLogInfo, "lookup");
  e.Conn(c).U("req", requestId).U("queries", l.queries.size()).U("matches", cMatches)
    .B("truncated", fMore).X("flags", l.flags);
  if (gOpt.logContents) {
    std::string qs;
    for (const std::string &q : l.queries) qs += (qs.empty() ? "" : ",") + q;
    e.S("query", qs);
  }
  SendEnvelope(ws, eph::kMsgLookupResult, requestId, pay.data(), pay.size());
}

// CANCEL (3.4): a request still being answered loses its unsent chunks and
// is answered ERROR 10; one answered completely, or unknown, gets nothing.
//
// Because a request is computed in blocks across loop turns (BeginRequest and
// AdvanceWork), erasing the stream here stops the COMPUTING too: the Work
// holds the only reference to the half-filled entry, which has not been put
// in the result cache and now never will be. That is 3.4's rule -- a
// cancelled request caches nothing, or a later identical question hits half
// an answer -- and it is why the `cancel` cap is advertised.
template <bool SSL>
static void HandleCancel(WebSocket<SSL, true, Conn> *ws, LoopCtx *lc, uint32_t requestId) {
  Conn *c = (Conn *)ws->getUserData();
  for (auto it = c->out.begin(); it != c->out.end(); ++it) {
    if (it->requestId != requestId) continue;
    uint32_t sent = it->nextRow, rows = it->nTimeRows, nObj = it->nObj;
    // What the cancel actually saved: the cells computed before it landed,
    // against the whole answer's. Read before the erase, which is what frees
    // the half-filled entry.
    uint64_t cells = it->work ? (uint64_t)it->work->iObj * rows + it->work->rowNext
                              : (uint64_t)nObj * rows;
    c->out.erase(it);
    c->cancels++;
    lc->m.cancels++;
    LogEvt(kLogInfo, "cancel").Conn(c).U("req", requestId).U("rows_sent", sent)
      .U("cells_computed", cells).U("cells", (uint64_t)nObj * rows).U("rows", rows);
    SendError(ws, requestId, eph::kErrCancelled, "cancelled");
    FlushStreams(ws);
    return;
  }
}

// One message off the wire (3.2, 3.3).
template <bool SSL>
static void HandleMessage(WebSocket<SSL, true, Conn> *ws,
                          std::string_view message, LoopCtx *lc) {
  Conn *c = (Conn *)ws->getUserData();
  const uint8_t *p = (const uint8_t *)message.data();
  const uint16_t kPreClose = c->proto ? 0 : eph::kErrFlagClosing;
  eph::Envelope env;
  std::string why;
  if (eph::ParseEnvelope(p, message.size(), &env, &why) != eph::kOk) {
    SendError(ws, 0, eph::kErrMalformed, ("bad envelope: " + why).c_str(), kPreClose);
    return;
  }
  if (env.version < eph::kProtoMin) {
    // An older client (3.3 step 5): refused in its own layout, then closed.
    SendLegacyVersionError(ws, env.version);
    return;
  }
  if (env.flags & eph::kEnvFlagZstd) {
    // No WELCOME advertises compression here; a compressed payload read as
    // raw would answer a question nobody asked.
    SendError(ws, env.requestId, eph::kErrUnsupported, "compressed payloads are not served",
              kPreClose);
    return;
  }
  if (env.payloadLen > (c->proto ? kMaxPayload : eph::kPreSessionMaxPayload)) {
    SendError(ws, env.requestId, eph::kErrLimits, "message larger than the payload limit", kPreClose);
    return;
  }
  if (eph::CheckRequestId(env, &why) != eph::kOk) {
    SendError(ws, env.requestId, eph::kErrMalformed, why.c_str(), kPreClose);
    return;
  }
  const uint8_t *pl = p + eph::kEnvelopeSize;
  const size_t len = env.payloadLen;
  if (c->proto == 0 && env.type != eph::kMsgHello && env.type != eph::kMsgPing) {
    // 3.3 step 6: HELLO fixes the session's version, and carries the token.
    LogEvt(kLogWarn, "hello.refuse").Conn(c).S("reason", "message_first").U("type", env.type);
    SendError(ws, env.requestId, eph::kErrMalformed, "HELLO first", eph::kErrFlagClosing);
    return;
  }

  switch (env.type) {
    case eph::kMsgHello: {
      lc->m.hellos++;
      c->hellos++;
      eph::Hello hello;
      eph::Outcome o = eph::ParseHello(pl, len, &hello, &why);
      if (o != eph::kOk) {
        SendError(ws, 0, o == eph::kMalformed ? eph::kErrMalformed : eph::kErrUnsupported,
                  ("HELLO: " + why).c_str(), kPreClose);
        return;
      }
      // What the client says it is, capped: it is the client's text.
      c->client = hello.clientName.substr(0, 80);
      if (c->proto != 0) {
        // 3.3 step 7: answered in the established session, not renegotiated.
        LogEvt(kLogDebug, "hello").Conn(c).U("proto", c->proto).S("repeat", "1");
        SendEnvelope(ws, eph::kMsgWelcome, 0, gWelcome.data(), gWelcome.size());
        return;
      }
      uint32_t session = std::min<uint32_t>(hello.protoMax, eph::kProtoVersion);
      if (session < std::max<uint32_t>(hello.protoMin, eph::kProtoMin)) {
        char sz[160];
        snprintf(sz, sizeof(sz), "this client needs protocol %u or newer; this server speaks "
                 "%u to %u", hello.protoMin, (unsigned)eph::kProtoMin, (unsigned)eph::kProtoVersion);
        LogEvt(kLogWarn, "hello.refuse").Conn(c).S("reason", "version")
          .U("proto_min", hello.protoMin).U("proto_max", hello.protoMax).S("client", c->client);
        c->proto = eph::kProtoVersion;
        SendError(ws, 0, eph::kErrVersion, sz, eph::kErrFlagClosing);
        return;
      }
      const char *szToken = hello.token.empty() ? "none" :
                            gTokens.count(hello.token) ? "accepted" : "unknown";
      if (!hello.token.empty() && gTokens.count(hello.token)) {
        c->budget = "t:" + hello.token;
      } else if (gOpt.requireToken) {
        LogEvt(kLogWarn, "hello.refuse").Conn(c).S("reason", "token")
          .S("token", szToken).U("proto_max", hello.protoMax).S("client", c->client);
        c->proto = (uint8_t)session;
        SendError(ws, 0, eph::kErrToken, hello.token.empty() ?
                  "this server requires a token" : "unknown token", eph::kErrFlagClosing);
        return;
      }
      c->proto = (uint8_t)session;
      c->clientCaps = hello.caps;
      SendEnvelope(ws, eph::kMsgWelcome, 0, gWelcome.data(), gWelcome.size());
      // The token is described, never written.
      LogEvt(kLogInfo, "hello").Conn(c)
        .U("proto_max", hello.protoMax).U("proto_min", hello.protoMin).U("proto", c->proto)
        .X("caps", hello.caps).U("build", hello.build).S("client", c->client)
        .S("token", szToken)
        .F("after_ms", std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now() - c->tOpen).count(), 1);
      break;
    }
    case eph::kMsgRequest:
      HandleRequest(ws, lc, env.requestId, pl, len);
      break;
    case eph::kMsgLookup:
      HandleLookup(ws, lc, env.requestId, pl, len);
      break;
    case eph::kMsgCancel:
      if (len != 0) {
        SendError(ws, env.requestId, eph::kErrMalformed, "CANCEL carries no payload");
        return;
      }
      HandleCancel(ws, lc, env.requestId);
      break;
    case eph::kMsgPing:
      if (len != 0) {
        SendError(ws, env.requestId, eph::kErrMalformed, "PING carries no payload", kPreClose);
        return;
      }
      SendEnvelope(ws, eph::kMsgPong, env.requestId, nullptr, 0);
      break;
    case eph::kMsgPong:
      break;   // the client's answer to a ping; liveness is uWS's job
    case eph::kMsgWelcome: case eph::kMsgData: case eph::kMsgError:
    case eph::kMsgLookupResult: case eph::kMsgSegData:
      SendError(ws, env.requestId, eph::kErrMalformed, "a server-to-client message from a client");
      break;
    default:
      SendError(ws, env.requestId, eph::kErrUnknownType, "unknown message type");
      break;
  }
}

template <bool SSL>
static void OnMessage(WebSocket<SSL, true, Conn> *ws, std::string_view message,
                      uWS::OpCode op) {
  if (op != uWS::OpCode::BINARY) {
    Conn *c = (Conn *)ws->getUserData();
    SendError(ws, 0, eph::kErrMalformed, "the protocol is binary frames only",
              c->proto ? 0 : eph::kErrFlagClosing);
    return;
  }
  HandleMessage(ws, message, tlc);
}

// Per-loop wiring: the thread-local LoopCtx pointer, the App with the
// WebSocket behavior (per-socket Conn, message handler,
// drain resuming the stream), and the listen socket (SO_REUSEPORT lets the
// kernel balance accepts across the per-thread listeners).
// ---------------------------------------------------------------------------
// 7. Operations
//
// Three GETs on the WebSocket port (uWS's WebSocket route yields a request
// without a WebSocket key, so plain GETs reach these): /healthz, the process
// answers; /readyz, it should be sent traffic -- every loop listening, an
// ephemeris found, not draining; /metrics, Prometheus text summed over the
// loops. Nothing in them names a client or a request (decision 0.3).
// ---------------------------------------------------------------------------

static std::vector<LoopCtx> *gLoops = nullptr;
static std::atomic<int> gListening{0};

static std::string MetricsText() {
  uint64_t connOpen = 0, connTotal = 0, hellos = 0, requests = 0, cells = 0,
           hits = 0, misses = 0, bytes = 0, bp = 0, cCompute = 0, uCompute = 0,
           lookups = 0, cancels = 0;
  uint64_t errors[kErrMax + 1] = {0}, buckets[kComputeBuckets] = {0};
  uint64_t refused = 0, helloTimeouts = 0;
  for (LoopCtx &lc : *gLoops) {
    connOpen += lc.m.connOpen; connTotal += lc.m.connTotal;
    hellos += lc.m.hellos; requests += lc.m.requests; cells += lc.m.cells;
    hits += lc.m.cacheHits; misses += lc.m.cacheMisses;
    lookups += lc.m.lookups; cancels += lc.m.cancels;
    bytes += lc.m.bytesSent; bp += lc.m.backpressureWaits;
    cCompute += lc.m.computeCount; uCompute += lc.m.computeMicros;
    for (int i = 0; i <= kErrMax; i++) errors[i] += lc.m.errors[i];
    refused += lc.m.refusedConns; helloTimeouts += lc.m.helloTimeouts;
    for (int b = 0; b < kComputeBuckets; b++) buckets[b] += lc.m.computeBucket[b];
  }
  char sz[256];
  std::string out;
  auto line = [&](const char *help, const char *type, const char *name,
                  uint64_t v) {
    snprintf(sz, sizeof(sz), "# HELP %s %s\n# TYPE %s %s\n%s %" PRIu64 "\n",
             name, help, name, type, name, v);
    out += sz;
  };
  char szSwe[256];
  swe_version(szSwe);
  snprintf(sz, sizeof(sz), "# HELP ephd_build_info Server and Swiss Ephemeris "
           "versions.\n# TYPE ephd_build_info gauge\nephd_build_info{server=\"%s\","
           "swisseph=\"%s\",protocol=\"%u\",tls=\"%d\"} 1\n", kServerVersion,
           szSwe, (unsigned)eph::kProtoVersion, gOpt.Tls() ? 1 : 0);
  out += sz;
  line("WebSocket connections open.", "gauge", "ephd_connections_open", connOpen);
  line("WebSocket connections accepted.", "counter", "ephd_connections_total", connTotal);
  line("HELLOs answered.", "counter", "ephd_hellos_total", hellos);
  line("REQUESTs answered with data.", "counter", "ephd_requests_total", requests);
  line("Cells (objects x rows) computed, cache misses only.", "counter",
       "ephd_cells_computed_total", cells);
  line("REQUESTs answered from the result cache.", "counter", "ephd_cache_hits_total", hits);
  line("REQUESTs computed.", "counter", "ephd_cache_misses_total", misses);
  line("LOOKUPs answered.", "counter", "ephd_lookups_total", lookups);
  line("REQUESTs cancelled while being answered.", "counter", "ephd_cancels_total", cancels);
  line("Bytes of WebSocket messages sent.", "counter", "ephd_bytes_sent_total", bytes);
  line("Times a stream waited for the client to read.", "counter",
       "ephd_backpressure_waits_total", bp);
  out += "# HELP ephd_errors_total ERRORs sent, by protocol version 4 code (A.19; 0 other).\n"
         "# TYPE ephd_errors_total counter\n";
  for (int i = 0; i <= kErrMax; i++) {
    snprintf(sz, sizeof(sz), "ephd_errors_total{code=\"%d\"} %" PRIu64 "\n", i,
             errors[i]);
    out += sz;
  }
  out += "# HELP ephd_compute_seconds Time computing one REQUEST (cache misses).\n"
         "# TYPE ephd_compute_seconds histogram\n";
  for (int b = 0; b < kComputeBuckets; b++) {
    snprintf(sz, sizeof(sz), "ephd_compute_seconds_bucket{le=\"%g\"} %" PRIu64 "\n",
             kComputeBucketsMs[b] / 1000.0, buckets[b]);
    out += sz;
  }
  snprintf(sz, sizeof(sz), "ephd_compute_seconds_bucket{le=\"+Inf\"} %" PRIu64 "\n"
           "ephd_compute_seconds_sum %.6f\nephd_compute_seconds_count %" PRIu64 "\n",
           cCompute, (double)uCompute / 1e6, cCompute);
  out += sz;
  line("Connections refused at the upgrade by --max-conns or --max-conns-per-ip.",
       "counter", "ephd_refused_connections_total", refused);
  line("Connections closed for sending no HELLO within --hello-seconds.",
       "counter", "ephd_hello_timeouts_total", helloTimeouts);
  line("1 while draining after SIGTERM.", "gauge", "ephd_draining",
       gDraining ? 1 : 0);
  return out;
}

// A peer's address as people write it. uWS's own text is the full
// uncompressed IPv6 form, and a server listening on every interface sees an
// IPv4 client as IPv4-mapped -- 0000:0000:0000:0000:0000:ffff:7f00:0001 for
// 127.0.0.1, measured -- so it is unreadable in the log and ungreppable for
// the address an operator knows. This is also the per-address limits' key,
// so one client is one key whichever way it arrived.
static std::string AddrText(std::string_view bin) {
  char sz[INET6_ADDRSTRLEN] = "";
  static const unsigned char kMapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
  if (bin.size() == 16 && memcmp(bin.data(), kMapped, 12) == 0)
    inet_ntop(AF_INET, bin.data() + 12, sz, sizeof(sz));
  else if (bin.size() == 16)
    inet_ntop(AF_INET6, bin.data(), sz, sizeof(sz));
  else if (bin.size() == 4)
    inet_ntop(AF_INET, bin.data(), sz, sizeof(sz));
  return sz;
}

// The HTTP routes at debug: a monitoring system asks every few seconds.
template <class Res>
static void LogHttp(Res *res, const char *path, int status) {
  LogEvt e(kLogDebug, "http");
  if (!e.On()) return;
  e.S("path", path).I("status", status);
  if (tlc) e.I("loop", tlc->index);
  e.S("addr", AddrText(res->getRemoteAddress()));
}

template <bool SSL>
static void WireOpsRoutes(uWS::TemplatedApp<SSL> *app) {
  app->get("/healthz", [](auto *res, auto *) {
    LogHttp(res, "/healthz", 200);
    res->writeHeader("Content-Type", "text/plain")->end("ok\n");
  });
  app->get("/readyz", [](auto *res, auto *) {
    const char *why = nullptr;
    if (gDraining) why = "draining\n";
    else if (!gHaveEphemeris) why = "no ephemeris directory found at startup\n";
    else if (gListening.load() < gOpt.threads) why = "not every loop is listening\n";
    LogHttp(res, "/readyz", why ? 503 : 200);
    if (why)
      res->writeStatus("503 Service Unavailable")
         ->writeHeader("Content-Type", "text/plain")->end(why);
    else
      res->writeHeader("Content-Type", "text/plain")->end("ready\n");
  });
  app->get("/metrics", [](auto *res, auto *) {
    LogHttp(res, "/metrics", 200);
    std::string body = MetricsText();
    res->writeHeader("Content-Type", "text/plain; version=0.0.4")->end(body);
  });
}

// The drain, SIGTERM's or SIGINT's. The signal thread only posts one defer
// per loop; everything after runs on the loop itself, on a timer, because
// a loop that has nothing left returns from run() and its thread-local
// uWS::Loop goes with it -- the first form drove the drain from its own
// thread and posted to loops that had already gone (a segfault at exit).
// Each loop: closes its listen socket, so new connections are refused;
// every 50 ms asks whether any connection still has an answer queued or
// bytes buffered unsent, and once none has, or --drain-seconds have passed,
// ends every WebSocket with 1001 and closes the timer. With no socket and
// no timer left, run() returns.
// At the deadline a connection with bytes still buffered is closed hard:
// end()'s close frame waits behind those bytes, and a reader that never
// reads never lets it out -- the first form left that loop running forever.
template <bool SSL>
static void EndAll(LoopCtx *lc, bool fHard) {
  std::vector<void *> socks(lc->socks.begin(), lc->socks.end());
  for (void *p : socks) {
    auto *ws = (WebSocket<SSL, true, Conn> *)p;
    if (fHard && ws->getBufferedAmount() > 0)
      ws->close();
    else
      ws->end(1001, "server going away");
  }
}

template <bool SSL>
static bool FIdle(LoopCtx *lc) {
  for (void *p : lc->socks) {
    auto *ws = (WebSocket<SSL, true, Conn> *)p;
    if (!((Conn *)ws->getUserData())->out.empty() || ws->getBufferedAmount() > 0)
      return false;
  }
  return true;
}

struct DrainTimer {
  LoopCtx *lc;
  std::chrono::steady_clock::time_point tEnd;
};

template <bool SSL>
static void DrainTick(us_timer_t *t) {
  DrainTimer *dt = (DrainTimer *)us_timer_ext(t);
  bool fLate = std::chrono::steady_clock::now() >= dt->tEnd;
  if (!FIdle<SSL>(dt->lc) && !fLate)
    return;
  if (fLate && !FIdle<SSL>(dt->lc))
    LogEvt(kLogWarn, "drain.cut").I("loop", dt->lc->index)
      .U("drain_s", gOpt.drainSeconds).U("open", dt->lc->socks.size())
      .Msg("loop %d still had answers unsent after %u s; closing",
           dt->lc->index, gOpt.drainSeconds);
  else
    LogEvt(kLogDebug, "drain.loop").I("loop", dt->lc->index)
      .U("closing", dt->lc->socks.size());
  EndAll<SSL>(dt->lc, fLate);
  // Closed here rather than at StartDrain: answers still being computed need
  // their blocks to finish, and the drain waits for exactly that.
  if (dt->lc->workTimer) {
    us_timer_close(dt->lc->workTimer);
    dt->lc->workTimer = nullptr;
  }
  us_timer_close(t);
}

template <bool SSL>
static void StartDrain(LoopCtx *lc) {
  if (lc->listenSock) {
    us_listen_socket_close(SSL, lc->listenSock);
    lc->listenSock = nullptr;
  }
  // Closed here, while the loop still runs to free it: after run() returns
  // nothing does, and ASan reported the two timers of a two-loop server
  // leaked at every exit.
  if (lc->helloTimer) {
    us_timer_close(lc->helloTimer);
    lc->helloTimer = nullptr;
  }
  us_timer_t *t = us_create_timer((us_loop_t *)lc->loop, 0, sizeof(DrainTimer));
  DrainTimer *dt = (DrainTimer *)us_timer_ext(t);
  dt->lc = lc;
  dt->tEnd = std::chrono::steady_clock::now() +
             std::chrono::seconds(gOpt.drainSeconds);
  us_timer_set(t, SSL ? DrainTick<true> : DrainTick<false>, 1, 50);
}

static void Drain(std::vector<LoopCtx> *loops) {
  const bool fTls = gOpt.Tls();
  uint64_t open = 0;
  for (LoopCtx &lc : *loops) open += lc.m.connOpen;
  LogEvt(kLogInfo, "drain.start").U("drain_s", gOpt.drainSeconds)
    .U("open", open)
    .Msg("draining: no new connections; answers in flight have %u s",
         gOpt.drainSeconds);
  for (LoopCtx &lc : *loops) {
    LoopCtx *plc = &lc;
    lc.loop->defer([plc, fTls]() {
      if (fTls) StartDrain<true>(plc); else StartDrain<false>(plc);
    });
  }
}

// TLS 1.2's suites, Mozilla's "intermediate" set: forward secrecy and AEAD
// only (section 6 says why uSockets needs to be told).
static const char kTlsCiphers[] =
  "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:"
  "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:"
  "ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305";

template <bool SSL>
static void WireApp(uWS::TemplatedApp<SSL> *app, LoopCtx *lc) {
  typename uWS::TemplatedApp<SSL>::template WebSocketBehavior<Conn> behavior;
  behavior.compression = uWS::DISABLED;
  behavior.maxPayloadLength = (unsigned)(kMaxPayload + 65536);
  behavior.idleTimeout = kIdleTimeoutSeconds;
  // No uWS limit: past it uWS DROPS every send, ERROR replies included,
  // so a client refused for having too many answers queued never heard it
  // and waited forever (the robustness gate's S4 hung on exactly that).
  // FlushStreams applies the ceiling to DATA chunks alone instead.
  behavior.maxBackpressure = 0;
  behavior.sendPingsAutomatically = true;
  // Conn is constructed once, by the upgrade handler below, moved into the
  // socket by uWS and destroyed on close; open must never construct one.
  // An open handler once did, over the live one, leaking its deque's
  // blocks -- about 575 bytes a connection, measured as RSS growing
  // linearly over 20000 connect/close cycles.
  behavior.message = [](WebSocket<SSL, true, Conn> *ws,
                        std::string_view message, uWS::OpCode op) {
    OnMessage(ws, message, op);
  };
  behavior.drain = [](WebSocket<SSL, true, Conn> *ws) {
    FlushStreams(ws);
  };
  // The connection caps, before a WebSocket exists: a refused client gets an
  // HTTP 503 with the reason, and nothing is allocated for it. Conn is
  // constructed here, once, with its address -- see the note on open.
  behavior.upgrade = [](uWS::HttpResponse<SSL> *res, uWS::HttpRequest *req,
                        us_socket_context_t *context) {
    std::string addr = AddrText(res->getRemoteAddress());
    if (const char *why = AdmitConnection(addr)) {
      tlc->m.refusedConns++;
      LogEvt(kLogWarn, "conn.refuse").I("loop", tlc->index).S("addr", addr)
        .S("reason", strstr(why, "address") ? "addr_limit" : "conn_limit")
        .S("msg", why);
      res->writeStatus("503 Service Unavailable")
         ->writeHeader("Content-Type", "text/plain")->end(why);
      return;
    }
    Conn conn;
    conn.id = ++gConnIds;
    conn.loop = tlc->index;
    conn.addr = addr;
    conn.budget = "a:" + addr;
    conn.fCountedAddr = true;
    res->template upgrade<Conn>(std::move(conn),
                                req->getHeader("sec-websocket-key"),
                                req->getHeader("sec-websocket-protocol"),
                                req->getHeader("sec-websocket-extensions"),
                                context);
  };
  // Counting and tracking only -- Conn is already constructed (see above).
  behavior.open = [](WebSocket<SSL, true, Conn> *ws) {
    tlc->m.connOpen++;
    tlc->m.connTotal++;
    tlc->socks.insert(ws);
    LogEvt(kLogInfo, "conn.open").Conn((Conn *)ws->getUserData())
      .S("scheme", SSL ? "wss" : "ws").U("open", tlc->m.connOpen.load());
  };
  // The connection's whole life on one line: how it ended, how long it
  // lasted, and what it did. uWS reports 1006 for a peer that vanished
  // without a close frame, and the code a server-side end() gave otherwise.
  behavior.close = [](WebSocket<SSL, true, Conn> *ws, int code,
                      std::string_view message) {
    tlc->m.connOpen--;
    tlc->socks.erase(ws);
    Conn *c = (Conn *)ws->getUserData();
    uint64_t unsent = 0;
    for (const Stream &st : c->out) unsent += st.nTimeRows - st.nextRow;
    LogEvt(kLogInfo, "conn.close").Conn(c).I("code", code)
      .S("reason", message.substr(0, 64))
      .F("dur_s", std::chrono::duration<double>(
           std::chrono::steady_clock::now() - c->tOpen).count(), 3)
      .U("proto", c->proto).U("reqs", c->reqs).U("hits", c->hits)
      .U("cells", c->cells).U("errors", c->errors).U("bytes_out", c->bytesOut)
      .U("unsent_rows", unsent).S("client", c->client);
  };
  WireOpsRoutes(app);
  app->template ws<Conn>("/*", std::move(behavior));
  // "listening" is the line a client may connect on. The "ephemeris path"
  // line above it in the log is printed before any loop binds the port,
  // and a client that connected on it was refused by a server that was
  // about to listen -- the suite's live group lost that race whenever the
  // run was slow enough. A port that cannot be bound is fatal, not
  // silent: a server with no listener answers nobody and looks alive.
  // (uSockets sets SO_REUSEPORT on every listener, so another server on
  // the port does NOT fail this; main() checks for one before any loop
  // starts.)
  auto onListen = [lc](us_listen_socket_t *sock) {
    if (sock == nullptr) {
      LogEvt(kLogError, "fatal").I("loop", lc->index).U("port", gOpt.port)
        .Msg("cannot listen on %s port %u (in use, or not an address of "
             "this host?)", gOpt.bind.empty() ? "every interface," :
             gOpt.bind.c_str(), (unsigned)gOpt.port);
      exit(1);
    }
    lc->listenSock = sock;
    // "evt=listen port=N" is what the gates and the suite wait for: the
    // first loop's at info, the rest at debug. Its first keys may not
    // change order.
    LogEvt(lc->index == 0 ? kLogInfo : kLogDebug, "listen")
      .U("port", gOpt.port).S("scheme", SSL ? "wss" : "ws")
      .S("bind", gOpt.bind.empty() ? "*" : gOpt.bind).I("loop", lc->index);
    if (++gListening == gOpt.threads)
      LogEvt(kLogInfo, "ready").U("loops", gOpt.threads);
  };
  if (gOpt.bind.empty())
    app->listen((int)gOpt.port, std::move(onListen));
  else
    app->listen(gOpt.bind, (int)gOpt.port, std::move(onListen));
}

template <bool SSL>
static void HelloSweep(us_timer_t *t) {
  LoopCtx *lc = *(LoopCtx **)us_timer_ext(t);
  auto tLimit = std::chrono::steady_clock::now() -
                std::chrono::seconds(gOpt.helloSeconds);
  std::vector<void *> late;
  for (void *p : lc->socks) {
    Conn *c = (Conn *)((WebSocket<SSL, true, Conn> *)p)->getUserData();
    if (c->proto == 0 && c->tOpen < tLimit) late.push_back(p);
  }
  for (void *p : late) {
    lc->m.helloTimeouts++;
    LogEvt(kLogWarn, "hello.timeout")
      .Conn((Conn *)((WebSocket<SSL, true, Conn> *)p)->getUserData())
      .U("hello_s", gOpt.helloSeconds);
    ((WebSocket<SSL, true, Conn> *)p)->end(1008, "no HELLO");
  }
}

static void SetupLoop(LoopCtx *lc) {
  tlc = lc;
  lc->loop = uWS::Loop::get();
  if (gOpt.Tls()) {
    uWS::SocketContextOptions tls;
    tls.cert_file_name = gOpt.tlsCert.c_str();
    tls.key_file_name = gOpt.tlsKey.c_str();
    tls.ssl_ciphers = kTlsCiphers;
    lc->sslApp = std::make_unique<uWS::SSLApp>(tls);
    // Checked at startup already (TlsCheckPair), so this is the files
    // changing in between, not a bad path.
    if (lc->sslApp->constructorFailed()) {
      LogEvt(kLogError, "fatal").I("loop", lc->index).S("cert", gOpt.tlsCert)
        .S("key", gOpt.tlsKey).Msg("TLS setup failed");
      exit(1);
    }
    WireApp(lc->sslApp.get(), lc);
  } else {
    lc->app = std::make_unique<uWS::App>();
    WireApp(lc->app.get(), lc);
  }
  // The HELLO deadline: once a second, close whatever connected and has not
  // said HELLO within --hello-seconds. NOT a fallthrough timer, though that
  // looks like the right kind for a sweep: uSockets' us_timer_close()
  // decrements the loop's live-poll count for every timer, and only a
  // non-fallthrough one ever incremented it, so closing a fallthrough timer
  // left the count at -1 and `while (num_polls)` never ended -- a drained
  // server that never exited. This one keeps the loop alive until the
  // drain closes it, which is what a drain waits for anyway.
  if (gOpt.helloSeconds) {
    us_timer_t *t = us_create_timer((us_loop_t *)lc->loop, 0, sizeof(LoopCtx *));
    lc->helloTimer = t;
    *(LoopCtx **)us_timer_ext(t) = lc;
    us_timer_set(t, gOpt.Tls() ? HelloSweep<true> : HelloSweep<false>, 1000, 1000);
  }
  // The work timer, created idle for the same reason the sweep is not a
  // fallthrough timer: it is armed and slowed rather than closed and
  // recreated, and the drain closes it once the loop has nothing left.
  {
    us_timer_t *t = us_create_timer((us_loop_t *)lc->loop, 0, sizeof(LoopCtx *));
    lc->workTimer = t;
    *(LoopCtx **)us_timer_ext(t) = lc;
    us_timer_set(t, gOpt.Tls() ? WorkTick<true> : WorkTick<false>, kWorkIdleMs, kWorkIdleMs);
  }
}

// Run the loop, then destroy its App on the same thread: the App frees
// socket contexts on its thread's uWS::Loop, which is thread-local and gone
// once the thread ends (destroying them from main() after a drain was a
// segfault).
static void RunLoop(LoopCtx *lc) {
  if (lc->sslApp) lc->sslApp->run();
  else lc->app->run();
  lc->sslApp.reset();
  lc->app.reset();
}

// ---------------------------------------------------------------------------
// 6. TLS
//
// uSockets floors the protocol at TLS 1.2 itself (crypto/openssl.c) and
// leaves TLS 1.3's suites at OpenSSL's defaults; the 1.2 list (kTlsCiphers,
// above SetupLoop) is set because uSockets applies its own only when DH
// parameters are given.
// ---------------------------------------------------------------------------

static std::string OpenSslError() {
  char sz[256] = "";
  unsigned long e = ERR_get_error();
  if (e) ERR_error_string_n(e, sz, sizeof(sz));
  ERR_clear_error();
  return sz;
}

// Load the pair into a scratch context and check everything a client would
// refuse or uSockets would fail on, naming the file: unreadable, not PEM, a
// key that does not match, a certificate already expired or not yet valid.
// *pszExpiry gets the leaf's notAfter for the log. The live contexts are
// only ever touched after this has passed, so a bad renewal never
// half-replaces a working pair.
static bool TlsCheckPair(const std::string &cert, const std::string &key,
                         std::string *pszWhy, std::string *pszExpiry) {
  SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
  bool fOk = false;
  if (ctx == nullptr) {
    *pszWhy = "cannot create a TLS context: " + OpenSslError();
  } else if (SSL_CTX_use_certificate_chain_file(ctx, cert.c_str()) != 1) {
    *pszWhy = "cannot read certificate " + cert + ": " + OpenSslError();
  } else if (SSL_CTX_use_PrivateKey_file(ctx, key.c_str(), SSL_FILETYPE_PEM) != 1) {
    // OpenSSL checks the pair while loading the key, so a mismatch
    // arrives here, as "key values mismatch", not at the check below.
    if (ERR_GET_REASON(ERR_peek_last_error()) == X509_R_KEY_VALUES_MISMATCH) {
      ERR_clear_error();
      *pszWhy = "private key " + key + " does not match certificate " + cert;
    } else {
      *pszWhy = "cannot read private key " + key + ": " + OpenSslError();
    }
  } else if (SSL_CTX_check_private_key(ctx) != 1) {
    *pszWhy = "private key " + key + " does not match certificate " + cert;
  } else {
    X509 *leaf = SSL_CTX_get0_certificate(ctx);
    const ASN1_TIME *notAfter = X509_get0_notAfter(leaf);
    char sz[64] = "?";
    BIO *bio = BIO_new(BIO_s_mem());
    if (bio && ASN1_TIME_print(bio, notAfter) == 1) {
      int n = BIO_read(bio, sz, sizeof(sz) - 1);
      sz[n > 0 ? n : 0] = '\0';
    }
    BIO_free(bio);
    *pszExpiry = sz;
    if (X509_cmp_current_time(notAfter) <= 0)
      *pszWhy = "certificate " + cert + " expired on " + sz;
    else if (X509_cmp_current_time(X509_get0_notBefore(leaf)) > 0)
      *pszWhy = "certificate " + cert + " is not valid yet";
    else
      fOk = true;
  }
  SSL_CTX_free(ctx);
  return fOk;
}

// SIGHUP: reload the certificate and key into every loop's live context.
// Each loop owns its SSLApp and so its own SSL_CTX (its WebSocket contexts
// share their parent's); only that loop's thread may touch it, so the
// signal thread checks the files and then defers the load to each loop.
// OpenSSL takes a new certificate on a context with live connections for
// the NEXT handshake and leaves established sessions alone -- which
// tools/ephsrv-tls.sh proves rather than trusts.
static void TlsReload(std::vector<LoopCtx> *loops) {
  std::string why, expiry;
  if (!TlsCheckPair(gOpt.tlsCert, gOpt.tlsKey, &why, &expiry)) {
    LogEvt(kLogError, "tls.reload.refused").S("cert", gOpt.tlsCert)
      .Msg("keeping the current certificate: %s", why.c_str());
    return;
  }
  LogEvt(kLogInfo, "tls.reload").S("cert", gOpt.tlsCert).S("key", gOpt.tlsKey)
    .S("expires", expiry);
  for (LoopCtx &lc : *loops) {
    LoopCtx *plc = &lc;
    lc.loop->defer([plc]() {
      SSL_CTX *ctx = (SSL_CTX *)plc->sslApp->getNativeHandle();
      if (SSL_CTX_use_certificate_chain_file(ctx, gOpt.tlsCert.c_str()) != 1 ||
          SSL_CTX_use_PrivateKey_file(ctx, gOpt.tlsKey.c_str(),
                                      SSL_FILETYPE_PEM) != 1 ||
          SSL_CTX_check_private_key(ctx) != 1)
        LogEvt(kLogError, "tls.reload.failed").I("loop", plc->index)
          .Msg("failed mid-reload (the files changed again?): %s",
               OpenSslError().c_str());
      else
        LogEvt(kLogDebug, "tls.reload.loop").I("loop", plc->index);
    });
  }
}

// Signals are taken by one thread in sigwait(), never by a handler: a
// handler may run on any loop's thread in the middle of anything, and the
// one uWS call safe from another thread is Loop::defer. Blocked in main()
// before any thread starts, so every thread inherits the mask.
static void SignalThread(std::vector<LoopCtx> *loops, sigset_t set) {
  for (;;) {
    int sig;
    if (sigwait(&set, &sig) != 0) continue;
    LogEvt(kLogInfo, "signal").S("sig", sig == SIGHUP ? "SIGHUP" :
                                  sig == SIGTERM ? "SIGTERM" : "SIGINT");
    if (sig == SIGHUP) {
      if (gOpt.Tls()) TlsReload(loops);
      else LogEvt(kLogWarn, "tls.reload.none")
             .Msg("no certificate to reload (plain ws://)");
    } else if (sig == SIGTERM || sig == SIGINT) {
      if (gDraining.exchange(true)) {
        LogEvt(kLogWarn, "exit").I("status", 1)
          .Msg("second signal while draining; exiting now");
        _exit(1);
      }
      Drain(loops);
    }
  }
}



// The dataset (3.4 WELCOME datasetId): the engine's version and the identity
// of every file the discovery found, by path, size and modification time --
// so replacing, adding or removing a sentinel file, or a new fork, is a new
// id, and every client cache keyed on the old one lets its answers go. A
// file Swiss opens that is not a sentinel (an asteroid's) is not seen; the
// documented way to pick up a changed tree is still a restart, and the id
// changes with the main files that every tree has.
static std::string DatasetIdOf(const EphDiscovery &disc, const char *szSwe) {
  uint64_t h = 14695981039346656037ULL;
  auto mix = [&h](const void *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
      h ^= ((const unsigned char *)p)[i];
      h *= 1099511628211ULL;
    }
  };
  mix(disc.resolved.data(), disc.resolved.size());
  for (const EphDir &d : disc.dirs)
    for (const char *hit : d.hits) {
      std::string file = d.dir + "/" + hit;
      struct stat st;
      if (stat(file.c_str(), &st) != 0) continue;
      int64_t v[3] = {(int64_t)st.st_size, (int64_t)st.st_mtim.tv_sec, (int64_t)st.st_mtim.tv_nsec};
      mix(file.data(), file.size() + 1);
      mix(v, sizeof(v));
    }
  // 3.4's shape: <engine>/<ephemeris>/<catalogs>#<8 hex>. This server has no
  // catalogs, so that field is empty.
  char sz[160];
  snprintf(sz, sizeof(sz), "%s/swiss %s/%s#%08x", kServerVersion, szSwe,
           disc.resolved.empty() ? "" : "files", (unsigned)(h ^ (h >> 32)));
  return sz;
}

// WELCOME (3.4) and its capabilities (A.3), which say truthfully what this
// server does: object kinds 0-3 and 5 (not elements yet), every observer,
// plane, form and frame, the correction masks Swiss can honour
// (ephswiss.h ApplyProfile), every A.11 zodiac and sidereal plane, UT1 and
// TT, the ayanamsa and delta T columns, Swiss's own delta T model, the rate
// budget when there is one, LOOKUP and the A.15 hypotheticals.
static void BuildWelcome(const EphDiscovery &disc, const char *szSwe) {
  gDatasetId = DatasetIdOf(disc, szSwe);
  gEngine = std::string("Swiss Ephemeris ") + szSwe + " files";
  // 3.4's source string shape: <engine> | <ephemeris> | <model>.
  gSource = std::string(kServerVersion) + " | Swiss Ephemeris " + szSwe + " | files";
  eph::Capabilities &c = gCaps;
  c.kinds = (1u << eph::kObjBody) | (1u << eph::kObjOrbitPoint) | (1u << eph::kObjStar) |
            (1u << eph::kObjHypothetical) | (1u << eph::kObjDesignation);
  c.observers = 0x1F;
  c.planes = 0x3;
  c.forms = 0x3;
  c.frames = 0xF;
  // 3.5a: corrections are honoured as sent, and the masks are advertised
  // per observer (A.3 0x0004). What Swiss does with them, MEASURED against
  // this server rather than inferred:
  //
  //   heliocentric and barycentric: a body answers masks 1, 3, 5 and 7
  //     IDENTICALLY -- plaus_iflag() turns aberration and deflection off
  //     inside the call whatever was asked. Only light time varies.
  //   planet-centred: all three terms are live and distinct. The earlier
  //     note here grouped this observer with the two above, and that was
  //     wrong: Venus from Jupiter moves between masks 1, 3, 5 and 7.
  //   swe_nod_aps() reads the bits itself, before any such normalisation,
  //     so an ORBIT POINT from a heliocentric observer does honour them --
  //     Jupiter's ascending node moves 5.8e-3 degrees between 7 and 1.
  //
  // That last line is why every mask used to be advertised for every
  // observer, with the narrowing left to a comment. The Prometheia
  // cross-test found what that costs: their client asked mask 7 from the
  // barycentre because WELCOME said it was served, and got a body back
  // with no deflection in it -- a 3.2 arcsec disagreement with their
  // server that looked like an arithmetic defect on one side.
  //
  // ephem.h's rule decides it: ADVERTISED IS PROMISED. A capability keyed
  // on the observer cannot say "live for orbit points, inert for bodies",
  // and between over-promising and under-promising, the honest failure is
  // the one that makes a client ask for less than it could have. So the
  // heliocentric and barycentric observers advertise only the masks whose
  // effect a body will actually see. META's corrApplied remains the
  // per-object truth and already said this correctly -- CorrectionsLive()
  // returns light time alone for a body at those observers -- and an
  // orbit point that still honours more reports so there.
  {
    const uint32_t obsDefl  = 0x03;   // geocentric, topocentric
    const uint32_t obsAberr = 0x13;   // ... and the planet-centred one
    const uint32_t obsAll   = 0x1F;   // ... and heliocentric, barycentric
    // Deflection is advertised for the geocentric and topocentric
    // observers ONLY. swe_calc_pctr() re-bases aberration on the centring
    // body but not deflection -- swi_deflect_light() takes no observer and
    // uses the Earth's -- so a planet-centred deflection is computed from
    // no observer's geometry and is wrong by up to half an arcsecond
    // (ephswiss.h's CorrectionsLive has the referee's numbers). Claiming
    // it and delivering that is worse than not offering it.
    c.corrMasks = {
      {obsAll,   0},
      {obsAll,   eph::kCorrLightTime},
      {obsAberr, eph::kCorrLightTime | eph::kCorrAberration},
      {obsDefl,  eph::kCorrLightTime | eph::kCorrDeflection},
      {obsDefl,  eph::kCorrMask},
    };
    // A.3 0x0014, the per-kind drop: our ONE exception. swe_nod_aps()
    // reads the correction bits before any normalisation, so an ORBIT
    // POINT from the Sun's centre or the barycentre honours them where a
    // BODY from the same observer cannot -- Jupiter's ascending node moves
    // 5.8e-3 degrees between mask 7 and mask 1. 0x0004 above is the
    // intersection over kinds and so cannot say this; before the drop this
    // server advertised the narrow truth and then accepted the wide mask
    // anyway, which worked and was a divergence nobody could read off the
    // specification.
    {
      const uint32_t obsSolar = 0x0C;   // heliocentric, barycentric
      const uint32_t kndPoint = 1u << eph::kObjOrbitPoint;
      c.corrByKind = {
        {obsSolar, kndPoint, eph::kCorrLightTime | eph::kCorrDeflection},
        {obsSolar, kndPoint, eph::kCorrLightTime | eph::kCorrAberration},
        {obsSolar, kndPoint, eph::kCorrMask},
      };
    }
  }
  c.orbitPoints = 0xF;
  // Mean, osculating, interpolated and the focal point -- not method 3
  // (osculating barycentric), whose mass 3.5a pins and Swiss's choice is
  // unverified (ephswiss.h says so too).
  c.orbitMethods = (1u << eph::kMethMean) | (1u << eph::kMethOsculating) |
                   (1u << eph::kMethInterpolated) | (1u << eph::kMethFocal);
  c.columns = kColumnsServed;
  for (int i = 0; i < eph::kZodiacTokenCount; i++) c.zodiacs.push_back(eph::kZodiacTokens[i]);
  c.siderealPlanes = 0x7;
  c.timeScales = (1u << eph::kTimeUT1) | (1u << eph::kTimeTT);
  c.deltaTModel = "swiss";
  c.fRate = gOpt.cellsPerSec != 0;
  c.cellsPerSec = gOpt.cellsPerSec;
  c.burst = gOpt.maxCells;
  c.lookupMax = kLookupMax;
  // 3.5a's rates bound (A.3 0x0013), measured rather than assumed: Swiss's
  // speeds against central differences of its own positions over +-0.001,
  // +-0.01 and +-0.0001 day at J2000, for the Sun, the Moon, Mars, Chiron
  // and Pluto. Angles agree to 2.4e-6 deg/day, inside 3.5a's 1e-5; DISTANCE
  // rates do not -- 2.2e-6 AU/day for Mars, 1.7e-5 for Chiron, 4.7e-5 for
  // Pluto, stable across the three step sizes, so real and not differencing
  // noise (it is the observer's acceleration over the light time). That is
  // past the 1e-6 AU/day tolerance, so the bound is advertised and every
  // object answered with speeds carries ratesApprox.
  c.fRatesBound = true;
  c.ratesDegPerDay = 3e-6f;
  c.ratesAuPerDay = 1e-4f;
  for (int i = 0; i < eph::kHypotheticalTokenCount; i++)
    c.hypotheticals.push_back(eph::kHypotheticalTokens[i]);
  eph::Welcome w;
  w.protoSession = eph::kProtoVersion;
  // Advertised is promised: a bit here is behaviour a client WILL take up,
  // so only what is implemented and exercised by a gate goes in.
  //   cancel      a request is computed in blocks of rows across loop turns,
  //               so a CANCEL stops the WORK and not merely the sending; a
  //               cancelled request caches nothing, and a large one does not
  //               starve the loop. tools/ephsrv-robust.sh gates all three.
  //   segments, elements (kind 4), deep sky, orbit method 3
  //               not implemented at all.
  //   zstd        not implemented.
  w.caps = eph::kCapF32 | eph::kCapCancel | eph::kCapLookup | eph::kCapInstantLists |
           eph::kCapPriority | eph::kCapDesignations | eph::kCapDeltaTTable;
  w.maxObjs = kMaxObjs;
  w.maxRows = kMaxRows;
  w.maxChunkRows = kMaxChunkRows;
  w.maxPayload = kMaxPayload;
  w.maxCells = gOpt.maxCells;
  w.maxProfiles = kMaxProfiles;
  w.serverName = kServerVersion;
  w.engine = WireText(gEngine.c_str());
  w.datasetId = gDatasetId;
  eph::EncodeCapabilities(c, &w.caps_);
  eph::EncodeWelcome(&gWelcome, w);
}

int main(int argc, char **argv) {
  if (!parseArgs(argc, argv, &gOpt)) {
    fprintf(stderr, "%s", kUsage);
    return 1;
  }
  // Before anything else can fail on it: a certificate problem named by
  // file, not a TLS context that uSockets silently could not build.
  if (gOpt.Tls()) {
    std::string why, expiry;
    if (!TlsCheckPair(gOpt.tlsCert, gOpt.tlsKey, &why, &expiry)) {
      LogEvt(kLogError, "fatal").S("cert", gOpt.tlsCert).S("key", gOpt.tlsKey)
        .S("msg", why);
      return 1;
    }
    LogEvt(kLogInfo, "tls.cert").S("cert", gOpt.tlsCert).S("key", gOpt.tlsKey)
      .S("expires", expiry);
  }

  if (!LoadTokens()) return 1;

  EphDiscovery disc = DiscoverEphemDirs();
  char szVersion[256];
  swe_version(szVersion);
  LogEvt(kLogInfo, "start").S("version", kServerVersion)
    .S("swisseph", szVersion).U("proto_min", eph::kProtoMin)
    .U("proto", eph::kProtoVersion).I("pid", getpid());
  LogEvt(kLogInfo, "config").U("port", gOpt.port)
    .S("bind", gOpt.bind.empty() ? "*" : gOpt.bind).B("tls", gOpt.Tls())
    .I("loops", gOpt.threads < 1 ? 1 : gOpt.threads).U("cache_mb", gOpt.cacheMb)
    .U("max_cells", gOpt.maxCells).U("max_conns", gOpt.maxConns)
    .U("max_conns_per_ip", gOpt.maxConnsPerAddr)
    .U("cells_per_sec", gOpt.cellsPerSec).U("hello_s", gOpt.helloSeconds)
    .U("idle_s", kIdleTimeoutSeconds).U("drain_s", gOpt.drainSeconds)
    .S("log_level", kLogLevelNames[gOpt.logLevel])
    .B("log_contents", gOpt.logContents);
  if (gOpt.logContents)
    LogEvt(kLogWarn, "log.contents")
      .Msg("--log-contents: every REQUEST's instants, bodies and settings "
           "are logged; not for a public server");
  // ERROR, not warn: this server answers ERROR 5 to every file-backed
  // request in this state, which is nearly every request there is, and a
  // warn is filtered out entirely by --log-level error -- leaving an
  // operator with a process that starts, listens, reports healthy and
  // serves nothing, with no line anywhere saying why. Raised after a
  // ':'-joined --ephe (the separator is ';') resolved to no directory at
  // all and the server came up looking fine.
  if (disc.resolved.empty())
    LogEvt(kLogError, "ephe").S("path", "")
      .Msg("no ephemeris directory found; every file-backed request will "
           "fail with ERROR 5 -- check --ephe (its list separator is ';')");
  else
    LogEvt(kLogInfo, "ephe").S("path", disc.resolved)
      .I("dirs", disc.cJoined);
  for (const EphDir &d : disc.dirs) {
    std::string hits;
    for (const char *h : d.hits) hits += (hits.empty() ? "" : ",") + std::string(h);
    LogEvt(kLogDebug, "ephe.dir").S("dir", d.dir).B("explicit", d.explicitDir)
      .S("sentinels", hits);
  }
  if (disc.cDropped)
    LogEvt(kLogWarn, "ephe.dropped").I("dirs", disc.cDropped)
      .Msg("directories dropped for SWE's 20-dir/242-byte budget");

  // The one process-global SWE call, before any context exists: contexts
  // inherit config at swe_ctx_new(). An empty path is legal -- strict
  // no-fallback means file-backed requests fail with ERROR 5 and their
  // serr text, and the discovery log above said why.
  swe_set_ephe_path(disc.resolved.c_str());
  gHaveEphemeris = !disc.resolved.empty();
  BuildWelcome(disc, szVersion);
  LogEvt(kLogInfo, "dataset").S("id", gDatasetId).S("engine", gEngine);

  // Refuse a port another server already listens on. Every listener
  // uSockets opens sets SO_REUSEPORT, so a second server -- a stale one, or
  // one started twice -- bound "successfully" and the kernel split the
  // connections between the two: measured, 10 clients got 8 answers from
  // one and 2 ERROR 5 from the other, and both logged "listening". So ask
  // the port first: if anything accepts a connection on it, it is taken.
  // (A trial bind without SO_REUSEPORT was the first form of this, and it
  // refused ports that only had client connections in TIME_WAIT on them --
  // which the ephemeral range makes common on a busy machine.)
  {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(gOpt.port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    // A server bound to one IPv4 address answers there, not on loopback.
    if (!gOpt.bind.empty())
      inet_pton(AF_INET, gOpt.bind.c_str(), &addr.sin_addr);
    if (fd >= 0 && connect(fd, (sockaddr *)&addr, sizeof(addr)) == 0) {
      close(fd);
      LogEvt(kLogError, "fatal").U("port", gOpt.port)
        .Msg("cannot listen on port %u (another server is listening on it)",
             (unsigned)gOpt.port);
      return 1;
    }
    if (fd >= 0) close(fd);
  }

  int nLoops = gOpt.threads < 1 ? 1 : gOpt.threads;
  // One context per loop. A loop is one thread and uses one context at a
  // time, so a second context in the same loop parallelised nothing: it
  // only opened every file a second time, and made the answer depend on
  // which of the loop's contexts a request happened to land on. The pool
  // was 2x cores split across loops -- loops past it got none and
  // answered every request ERROR 4 (--threads 64 on 12 cores: 14 of 20
  // connections), and with one loop the soak gate watched the server's
  // descriptors climb by 43 as 24 contexts each opened the same files.
  int totalCtx = nLoops;
  // --cache-mb is the TOTAL budget; each loop gets an equal share, since
  // the kernel spreads connections across loops and one loop cannot answer
  // from another's cache.
  size_t cacheBytesPerLoop = ((size_t)gOpt.cacheMb * 1024 * 1024) / (size_t)nLoops;
  std::vector<LoopCtx> loops(nLoops);
  for (int i = 0; i < nLoops; i++) {
    loops[i].index = i;
    loops[i].cache = eph::ResultCache(cacheBytesPerLoop);
    int n = totalCtx / nLoops + (i < totalCtx % nLoops ? 1 : 0);
    for (int j = 0; j < n; j++) {
      swe_ctx *ctx = swe_ctx_new();
      if (ctx == nullptr) {
        LogEvt(kLogError, "ctx.alloc").I("loop", i)
          .Msg("swe_ctx_new() failed; reducing the pool");
        break;
      }
      loops[i].pool.push_back(ctx);
    }
  }

  gOpt.threads = nLoops;
  gLoops = &loops;
  {
    // Each connection is a descriptor, and running out looks like a server
    // that stopped accepting. Raise the soft limit to the hard one -- a
    // container's soft limit was 1024 (measured, Docker on this machine),
    // a tenth of --max-conns' default -- and say what it is, and when it is
    // still below the connection cap.
    rlimit rl {};
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
      if (rl.rlim_cur < rl.rlim_max) {
        rlimit rlUp = rl;
        rlUp.rlim_cur = rl.rlim_max;
        if (setrlimit(RLIMIT_NOFILE, &rlUp) == 0) rl = rlUp;
      }
      bool fLow = gOpt.maxConns && rl.rlim_cur != RLIM_INFINITY &&
                  rl.rlim_cur < (rlim_t)gOpt.maxConns + 256;
      LogEvt e(fLow ? kLogWarn : kLogInfo, "nofile");
      e.U("soft", (uint64_t)rl.rlim_cur).U("hard", (uint64_t)rl.rlim_max);
      if (fLow)
        e.Msg("the open-file limit is below --max-conns %u plus the ephemeris "
              "files; connections past it will be refused by the kernel, not "
              "by the server", gOpt.maxConns);
    }
  }
  // Blocked here, before any thread exists, so every loop inherits the mask
  // and only SignalThread ever receives these.
  sigset_t sigs;
  sigemptyset(&sigs);
  sigaddset(&sigs, SIGHUP);
  sigaddset(&sigs, SIGTERM);
  sigaddset(&sigs, SIGINT);
  pthread_sigmask(SIG_BLOCK, &sigs, nullptr);
  // A loop's uWS::Loop pointer is set by SetupLoop on its own thread, and
  // TlsReload defers onto it: the signal thread must not start before every
  // loop has one.
  std::atomic<int> cSetUp{0};
  std::vector<std::thread> threads;
  for (int i = 1; i < nLoops; i++) {
    threads.emplace_back([&loops, &cSetUp, i] {
      SetupLoop(&loops[i]);
      cSetUp++;
      RunLoop(&loops[i]);
    });
  }
  SetupLoop(&loops[0]);
  while (cSetUp.load() < nLoops - 1)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  std::thread(SignalThread, &loops, sigs).detach();
  RunLoop(&loops[0]);
  for (std::thread &t : threads) t.join();

  if (gDraining)
    LogEvt(kLogInfo, "exit").I("status", 0).Msg("drained; exiting");
  for (LoopCtx &lc : loops)
    for (swe_ctx *ctx : lc.pool) {
      swe_close_r(ctx);   // releases file handles; never swe_close(), which
      swe_ctx_free(ctx);  // resets the process-wide config (fork contract)
    }
  return 0;
}
