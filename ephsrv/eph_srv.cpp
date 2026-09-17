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
//   3. request execution (plan 5) -- one swe_ctx per request, pure function,
//      answered from the loop's result cache when the same question was
//      asked before (eph_cache.h)
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

#include <App.h>

#include <swephexp.h>

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

// Log levels, most severe first: a line is written when its level is at or
// above --log-level's.
enum LogLevel { kLogError = 0, kLogWarn = 1, kLogInfo = 2, kLogDebug = 3 };
static const char *const kLogLevelNames[] = {"error", "warn", "info", "debug"};

struct Options {
  uint16_t port = eph::kDefaultPort;
  int threads = (int)std::thread::hardware_concurrency();
  uint32_t cacheMb = 256;      // result cache, TOTAL across loops; 0 disables
  uint32_t maxCells = eph::kMaxCellsDefault;  // objects x rows per REQUEST
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
// 3. Request execution (plan 5)
//
// A request is a pure function of its payload: build its canonical key,
// answer from the loop's result cache if it has been asked before, else for
// each time row and object call the thread-safe fork on one private
// context and store the result. A per-row failure is recorded as NaN in
// that row's columns, with the first failed row's serr in the object's
// metadata, and never fails the request; a whole-request failure is
// ERROR 5 with the SWE serr text
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
  // The computed columns (object-major nObj*nTimeRows*6 f64) and the nObj
  // metadata records, shared with the loop's result cache: a hit hands
  // out the cached entry, a miss the one just computed and inserted. The
  // stream keeps the entry alive however the cache turns over meanwhile.
  std::shared_ptr<const eph::CacheEntry> result;
  std::vector<uint8_t> buf;     // chunk scratch, sized once, reused
  // For the request's log line, written as its last chunk goes out.
  std::chrono::steady_clock::time_point tReq;  // REQUEST received
  double computeMs = 0.0;       // 0 on a cache hit
  bool fHit = false;
  uint32_t stalls = 0;          // times it waited for the client to read
  uint64_t bytes = 0;           // DATA bytes sent
  std::string contents;         // --log-contents only: what was asked
  bool fLogged = false;
};

struct Conn {
  std::deque<Stream> out;       // completed streams, FIFO
  // Protocol 3. proto is the session's version, fixed by HELLO; 0 before
  // it, when only HELLO is served. addr is the peer's address as text;
  // budget is what the cell budget is keyed on -- "t:" and the token when
  // HELLO gave a known one, "a:" and the address otherwise.
  uint8_t proto = 0;
  std::chrono::steady_clock::time_point tOpen = std::chrono::steady_clock::now();
  std::string addr;
  std::string budget;
  bool fCountedAddr = false;    // holds a place in gConnsByAddr
  // For the log: an id unique for the process's life, the loop, what HELLO
  // said the client is, and the connection's totals for its close line.
  uint64_t id = 0;
  int loop = -1;
  std::string client;           // HELLO's version string
  uint32_t hellos = 0;
  uint64_t reqs = 0, hits = 0, cells = 0, errors = 0, bytesOut = 0;

  // The place is released when the Conn goes, however it goes -- a client
  // that drops between the upgrade and open never reaches the close handler.
  // Moving hands the place over, since uWS moves the upgrade's Conn into the
  // socket and then destroys the original.
  Conn() = default;
  Conn(Conn &&o) noexcept
    : out(std::move(o.out)), proto(o.proto), tOpen(o.tOpen),
      addr(std::move(o.addr)), budget(std::move(o.budget)),
      fCountedAddr(o.fCountedAddr), id(o.id), loop(o.loop),
      client(std::move(o.client)), hellos(o.hellos), reqs(o.reqs),
      hits(o.hits), cells(o.cells), errors(o.errors), bytesOut(o.bytesOut) {
    o.fCountedAddr = false;
  }
  Conn &operator=(Conn &&) = delete;
  ~Conn();
};

// Answers a connection may have computed and not yet read. A client that
// sends requests and never reads used to make the server compute and hold
// every one -- 60 MB of columns for each 64-body 20000-row window, kept
// alive by its stream after the cache let it go -- so past this many the
// request is refused with ERROR 2 before anything is computed.
static const size_t kMaxQueuedStreams = 4;

// Bytes a connection may have buffered unsent before its DATA chunks wait
// for drain. Applied here rather than as uWS's maxBackpressure; see
// SetupLoop.
static const unsigned kStreamBackpressure = 4 * 1024 * 1024;

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
//
// The JPL file is set only when it changes (EPHEMERIS_REVIEW.md S12):
// swe_set_jpl_file_r() closes every file the context has open and rebuilds
// its delta-t and leap-second tables on each call, so a JPL request for
// every row of a long animation paid that per request. A loop's pool holds
// one context, so the name of its open JPL file is loop state; a request
// naming a different file pays the close once. A file left open across
// requests is read only by calls that name it (SWIEPH's delta-t comes from
// the moon file, ctx->fidat[SEI_FILE_MOON]), so no later answer moves.
static void ApplyRequestConfig(swe_ctx *ctx, std::string *pjplOpen,
                               const eph::Request &req) {
  if (req.iflag & SEFLG_SIDEREAL)
    swe_set_sid_mode_r(ctx, (int32_t)req.sidMode, req.sidT0, req.sidAyanOff);
  if (req.iflag & SEFLG_TOPOCTR)
    swe_set_topo_r(ctx, req.topoLon, req.topoLat, req.topoElv);
  if (req.iflag & SEFLG_JPLEPH && *pjplOpen != req.jplFile) {
    swe_set_jpl_file_r(ctx, req.jplFile);
    *pjplOpen = req.jplFile;
  }
}

// Compute one (object, row) cell. Returns the SWE return flag; on failure
// serr is filled. kind 0 bodies go through swe_calc / swe_calc_pctr when
// the request names a center body; kind 1 fixed stars through swe_fixstar
// (which also resolves the name in place); kind 2 through swe_nod_aps.
//
// The instant is UT unless the request carries kIflagTimeTT, in which case
// it is TT and the ET entry points get it exactly as sent (ephproto.h says
// why). swe_calc_pctr_r and swe_nod_aps_r have no UT form of their own
// that this uses: a UT instant is converted with swe_deltat_ex_r, the
// same conversion swe_calc_ut_r makes -- the first increment handed
// swe_calc_pctr_r the UT instant unconverted, which was a delta-t of
// error on every centered request.
static int32_t ComputeCell(swe_ctx *ctx, const eph::Request &req,
                           const eph::ObjSpec &obj, double jd,
                           uint32_t iflag, double xx[6], char *serr,
                           const char **pName, char *nameBuf, size_t nameCap) {
  serr[0] = '\0';
  *pName = nullptr;
  const bool fTT = (req.iflag & eph::kIflagTimeTT) != 0;
  const bool fCenter = (req.iflag & eph::kIflagCenter) != 0 || req.center != 0;
  // The ET instant, for the entry points that take one.
  auto jdEt = [&]() -> double {
    return fTT ? jd : jd + swe_deltat_ex_r(ctx, jd, (int32_t)iflag, nullptr);
  };
  if (obj.kind == eph::kObjStar) {
    if (fCenter) {
      // No pctr form exists for fixed stars; say so rather than guess.
      snprintf(serr, 256, "center body not supported for fixed stars");
      return -1;
    }
    snprintf(nameBuf, nameCap, "%s", obj.name);
    int32_t ret = fTT ?
      swe_fixstar_r(ctx, nameBuf, jd, (int32_t)iflag, xx, serr) :
      swe_fixstar_ut_r(ctx, nameBuf, jd, (int32_t)iflag, xx, serr);
    if (ret >= 0) *pName = nameBuf;   // resolved full star name
    return ret;
  }
  int32_t ret;
  if (obj.kind == eph::kObjNodAps) {
    if (fCenter) {
      snprintf(serr, 256, "center body not supported for nodes and apsides");
      return -1;
    }
    double xnasc[6], xndsc[6], xperi[6], xaphe[6];
    int32_t method = obj.method == eph::kNodOscu ? SE_NODBIT_OSCU : SE_NODBIT_MEAN;
    ret = swe_nod_aps_r(ctx, jdEt(), (int32_t)obj.id, (int32_t)iflag, method,
                        xnasc, xndsc, xperi, xaphe, serr);
    if (ret >= 0) {
      const double *px = obj.point == eph::kPntNorthNode ? xnasc :
                         obj.point == eph::kPntSouthNode ? xndsc :
                         obj.point == eph::kPntPerihelion ? xperi : xaphe;
      for (int c = 0; c < 6; c++) xx[c] = px[c];
      ret = (int32_t)iflag;   // swe_nod_aps_r returns OK, not the flags
    }
  } else if (fCenter) {
    ret = swe_calc_pctr_r(ctx, jdEt(), (int32_t)obj.id, req.center, (int32_t)iflag, xx, serr);
  } else if (fTT) {
    ret = swe_calc_r(ctx, jd, (int32_t)obj.id, (int32_t)iflag, xx, serr);
  } else {
    ret = swe_calc_ut_r(ctx, jd, (int32_t)obj.id, (int32_t)iflag, xx, serr);
  }
  if (ret >= 0) {
    // AS_MAXCH, the fork's contract: a name from seasnam.txt or
    // seorbel.txt is copied in with strcpy at up to that length.
    char nm[AS_MAXCH];
    swe_get_planet_name_r(ctx, (int)obj.id, nm);
    snprintf(nameBuf, nameCap, "%.*s", (int)nameCap - 1, nm);
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
// pool (pool total 2x cores) and a private LRU result cache (its share of
// --cache-mb), so nothing on the request path is shared between threads.
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
  std::atomic<uint64_t> errors[eph::kErrMax + 1] = {};  // by code; 0 other
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
  std::unordered_set<void *> socks;
  std::atomic<uint64_t> pendingStreams{0};  // set by a drain's check
  uWS::Loop *loop = nullptr;
  std::unique_ptr<uWS::App> app;        // plain ws://, or
  std::unique_ptr<uWS::SSLApp> sslApp;  // wss:// under --tls-cert
  std::vector<swe_ctx *> pool;   // private slice, never shared
  size_t nextCtx = 0;
  // The per-loop LRU result cache (eph_cache.h), keyed on the canonical
  // REQUEST, hashed FNV-1a, capped at this loop's share of --cache-mb.
  eph::ResultCache cache;
  // Wall time of the last ExecuteRequest's compute, for the verbose log
  // and the bench: zero on a hit.
  double lastComputeMs = 0.0;
  bool lastWasHit = false;
  // A request with SE_SIDBIT_PREC_ORIG in its sidereal mode changed this
  // loop's context's precession and nutation models, and Swiss keeps such
  // a change (upstream too: it is one process's configuration there). In a
  // context that serves every request on the loop it leaked into the next
  // request, tropical ones included -- 31 to 37 arcseconds, measured
  // (EPHEMERIS_REVIEW.md F8). The next request puts the models back first.
  bool fModelsTouched = false;

  swe_ctx *TakeContext() {
    if (pool.empty()) return nullptr;
    swe_ctx *ctx = pool[nextCtx % pool.size()];
    nextCtx++;
    return ctx;
  }

  // The JPL file the pool's one context has open (EPHEMERIS_REVIEW.md
  // S12): the setter runs only when a request names a different one.
  std::string jplOpen;
};

static thread_local LoopCtx *tlc = nullptr;   // this thread's loop state
static std::atomic<uint64_t> gConnIds{0};     // the log's conn= ids

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

// Heartbeats are uWS's own: sendPingsAutomatically sends WebSocket protocol
// pings on the idle timeout's cadence and closes silent connections (plan
// 4.7's intent; QWebSocket answers protocol pings by itself, so a Qt client
// never sees an app-level PING). The app-level kMsgPing/kMsgPong types stay
// in the protocol for clients without automatic pong.
static const int kIdleTimeoutSeconds = 30;

template <bool SSL>
static typename WebSocket<SSL, true, Conn>::SendStatus
SendEnvelope(WebSocket<SSL, true, Conn> *ws, uint16_t type,
             uint32_t requestId, const void *payload,
             size_t payloadLen, uint8_t flags = 0) {
  std::vector<uint8_t> msg(eph::kEnvelopeSize + payloadLen);
  // Every message in the session's version, once HELLO has fixed one.
  uint8_t version = ((Conn *)ws->getUserData())->proto;
  eph::writeEnvelope(msg.data(), type, requestId, flags, (uint32_t)payloadLen,
                     version ? version : eph::kProtoVersion);
  if (payloadLen) memcpy(msg.data() + eph::kEnvelopeSize, payload, payloadLen);
  auto st = ws->send(std::string_view((char *)msg.data(), msg.size()),
                     uWS::OpCode::BINARY);
  if (st != WebSocket<SSL, true, Conn>::SendStatus::DROPPED) {
    if (tlc) tlc->m.bytesSent += msg.size();
    ((Conn *)ws->getUserData())->bytesOut += msg.size();
  }
  return st;
}

// The protocol's ERROR codes by name, for the log's name= key.
static const char *ErrName(int32_t code) {
  switch (code) {
    case eph::kErrBad: return "bad";
    case eph::kErrLimits: return "limits";
    case eph::kErrUnknown: return "unknown_type";
    case eph::kErrInternal: return "internal";
    case eph::kErrEphemeris: return "ephemeris";
    case eph::kErrRateLimited: return "rate_limited";
    case eph::kErrToken: return "token";
    case eph::kErrVersion: return "version";
    default: return "other";
  }
}

template <bool SSL>
static void SendError(WebSocket<SSL, true, Conn> *ws, uint32_t requestId,
                      int32_t code, const char *text) {
  uint8_t fixed[8];
  eph::putU32(fixed, requestId);
  eph::putU32(fixed + 4, (uint32_t)code);
  size_t n = text ? strlen(text) : 0;
  std::vector<uint8_t> payload(8 + n + 1);
  memcpy(payload.data(), fixed, 8);
  memcpy(payload.data() + 8, text ? text : "", n + 1);
  if (tlc) tlc->m.errors[code > 0 && code <= eph::kErrMax ? code : 0]++;
  Conn *c = (Conn *)ws->getUserData();
  c->errors++;
  // Every ERROR text is fixed or a count, never a request's contents: Swiss's
  // per-row text, which names the instant, goes only to the client, inside
  // the DATA metadata. Keep it that way (production plan decision 0.3) --
  // a new ERROR that quotes a request must not be logged with its text.
  // The server's own failures are errors; a client's mistakes, limits and
  // a missing ephemeris are warnings about that client.
  LogEvt(code == eph::kErrInternal ? kLogError : kLogWarn, "error")
    .Conn(c).U("req", requestId).I("code", code).S("name", ErrName(code))
    .S("msg", std::string_view(text ? text : "").substr(0, 160));
  SendEnvelope(ws, eph::kMsgError, requestId, payload.data(), payload.size());
}

// Execute one REQUEST into a Stream. Returns false for a whole-request
// failure and fills errCode/errText (plan 4.6/4.7). The only whole-request
// failure is the missing-ephemeris-path case: with no ephemeris directory
// found at startup and SEFLG_SWIEPH forced, every object fails with
// the same file-not-found serr, so the first probe's serr becomes the
// request-level ERROR 5 text and nothing is streamed. A per-row failure
// is never a request failure: that row's six columns are NaN, the object
// keeps retFlag >= 0 while any row computed, and its metadata carries the
// first failed row's serr (plan 4.5, protocol 2).
static bool ExecuteRequest(LoopCtx *lc, const eph::Request &req,
                           Stream *stream, int32_t *errCode,
                           std::string *errText) {
  if (!gHaveEphemeris) {
    *errCode = eph::kErrEphemeris;
    *errText = "no ephemeris directory found at startup; start the server "
               "with --ephe or configure the client's -Yi1";
    return false;
  }
  const std::string key = eph::cacheKeyOf(req);
  if (auto hit = lc->cache.get(key)) {
    stream->result = hit;
    lc->lastWasHit = true;
    lc->lastComputeMs = 0.0;
    return true;
  }
  lc->lastWasHit = false;
  auto t0 = std::chrono::steady_clock::now();

  swe_ctx *ctx = lc->TakeContext();
  if (ctx == nullptr) {
    *errCode = eph::kErrInternal;
    *errText = "no swe context available";
    return false;
  }

  if (lc->fModelsTouched) {
    // All zeros is a fresh context's state: every model at its default.
    // (An empty string would set this version's explicit model list,
    // which is not guaranteed to be the same bits.)
    swe_set_astro_models_r(ctx, (char *)"0,0,0,0,0,0,0,0", 0);
    lc->fModelsTouched = false;
  }
  ApplyRequestConfig(ctx, &lc->jplOpen, req);
  if ((req.iflag & SEFLG_SIDEREAL) && (req.sidMode & SE_SIDBIT_PREC_ORIG))
    lc->fModelsTouched = true;

  const uint32_t nObj = stream->nObj, nTime = stream->nTimeRows;
  auto entry = std::make_shared<eph::CacheEntry>();
  entry->nObj = nObj;
  entry->nTimeRows = nTime;
  entry->cols.assign((size_t)nObj * nTime * 6, 0.0);
  entry->meta.assign((size_t)nObj * eph::kDataMetaSize, 0);

  // SWE's iflag is int32 and lives in the low 32 bits; the high half is the
  // protocol's (kIflagTimeTT, kIflagCenter; ephproto.h) and is read by
  // ComputeCell from the request, never handed to SWE. SEFLG_SWIEPH is
  // forced (plan 4.4) and SEFLG_SPEED added if absent, so speeds arrive in
  // the columns and clients never derive them.
  int32_t iflag = (int32_t)(uint32_t)(req.iflag & 0xFFFFFFFFu) | SEFLG_SWIEPH;
  iflag |= SEFLG_SPEED;

  char serr[256];
  for (uint32_t o = 0; o < nObj; o++) {
    const eph::ObjSpec &obj = req.objs[o];
    char nameBuf[eph::kObjNameMax];
    const char *name = nullptr;
    char nameKept[eph::kObjNameMax] = {0};
    int32_t retRow0 = 0;
    bool fFailed = false, fSawSuccess = false;
    char failSerr[eph::kSerrMax] = {0};
    for (uint32_t r = 0; r < nTime; r++) {
      // Row r's instant, UT or TT as the request says; the client computes
      // the same expression to find its rows, so it must not change.
      double jd = req.jdStart +
        (double)((uint64_t)r * (uint64_t)req.stepSeconds) / 86400.0;
      double xx[6];
      int32_t ret = ComputeCell(ctx, req, obj, jd, (uint32_t)iflag, xx, serr,
                                &name, nameBuf, sizeof(nameBuf));
      if (ret < 0) {
        // Protocol 2 (EPHEMERIS_REVIEW.md S9): a row that fails is NaN in
        // all six columns, and the rows that computed are real. Version 1
        // failed the object for the whole window here, so a window
        // reaching past a file's end lost every frame of it.
        double *dst = entry->cols.data() + ((size_t)o * nTime + r) * 6;
        for (int c = 0; c < 6; c++) dst[c] = NAN;
        fFailed = true;
        if (failSerr[0] == '\0')
          snprintf(failSerr, sizeof(failSerr), "%.63s", serr);
        continue;
      }
      if (!fSawSuccess) {
        fSawSuccess = true;
        retRow0 = ret;
        // ComputeCell clears the name on every call, so a later failed
        // row would otherwise leave an object that did compute unnamed.
        if (name != nullptr)
          snprintf(nameKept, sizeof(nameKept), "%s", name);
      }
      double *dst = entry->cols.data() + ((size_t)o * nTime + r) * 6;
      for (int c = 0; c < 6; c++) dst[c] = xx[c];
    }
    eph::writeDataMeta(entry->meta.data() + (size_t)o * eph::kDataMetaSize,
                       fSawSuccess ? retRow0 : -1, retRow0,
                       fFailed ? failSerr : nullptr, fSawSuccess ? nameKept : nullptr);
  }
  lc->lastComputeMs = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - t0).count();
  // Per-object failures are cached with the rest: the answer is a pure
  // function of the files on the configured path, and a file that appears
  // later is picked up the documented way, by restarting the server.
  stream->result = entry;
  lc->cache.put(key, std::move(entry));
  return true;
}

// What a REQUEST asked, as log keys, for --log-contents: the window, the
// flags and every object -- a body by its Swiss id, a star as s:name, a
// node or apsis as n:id/point/method.
static std::string RequestContents(const eph::Request &req) {
  LogEvt e;
  e.F("jd", req.jdStart, 6).U("step_s", req.stepSeconds)
    .B("tt", (req.iflag & eph::kIflagTimeTT) != 0).X("iflag", req.iflag);
  if (req.iflag & eph::kIflagCenter) e.I("center", req.center);
  if (req.iflag & SEFLG_SIDEREAL)
    e.I("sid_mode", req.sidMode).F("sid_t0", req.sidT0, 6)
     .F("sid_ayan", req.sidAyanOff, 6);
  if (req.iflag & SEFLG_TOPOCTR)
    e.F("topo_lon", req.topoLon, 6).F("topo_lat", req.topoLat, 6)
     .F("topo_elv", req.topoElv, 1);
  if (req.jplFile[0]) e.S("jpl", req.jplFile);
  std::string objs;
  for (const eph::ObjSpec &o : req.objs) {
    if (!objs.empty()) objs += ',';
    if (o.kind == eph::kObjStar) objs += std::string("s:") + o.name;
    else if (o.kind == eph::kObjNodAps)
      objs += "n:" + std::to_string(o.id) + "/" + std::to_string(o.point) +
              "/" + std::to_string(o.method);
    else objs += std::to_string(o.id);
  }
  e.S("bodies", objs);
  return e.Take();
}

// One line per REQUEST answered, at info: its size, whether the cache had
// it, how long computing and delivering it took, and the loop's cache.
// ERRORs have their own line (SendError), so every REQUEST gets exactly
// one of the two.
template <bool SSL>
static void LogRequest(WebSocket<SSL, true, Conn> *ws, const Stream &s,
                       uint64_t bytes) {
  LogEvt e(kLogInfo, "req");
  if (!e.On()) return;
  Conn *c = (Conn *)ws->getUserData();
  e.Conn(c).U("req", s.requestId).U("objs", s.nObj).U("rows", s.nTimeRows)
    .U("cells", (uint64_t)s.nObj * s.nTimeRows).U("prec", s.precision == eph::kPrecF32 ? 32 : 64)
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

// Stream completed chunks to the client, honoring uWS backpressure: stop
// at the first BACKPRESSURE and resume from the drain callback. uWS's
// BACKPRESSURE means the frame WAS taken and buffered -- only DROPPED means
// it was not -- so a chunk that returns BACKPRESSURE counts as sent. It
// used not to, and every such chunk went out twice: a Qt client counting
// rows marked the window done a chunk early and read zeros for the rest
// (64 bodies x 20000 rows to a reader asleep 3 s: 41 messages, 20500 rows). Chunks are
// chunkRows-sized pieces, ascending chunkIndex, contiguous row ranges. The
// columns are object-major (nObj blocks of nTime*6), so writeDataChunk
// gathers each object's own row range out of the full column array.
template <bool SSL>
static void FlushStreams(WebSocket<SSL, true, Conn> *ws) {
  Conn *c = (Conn *)ws->getUserData();
  while (!c->out.empty()) {
    Stream &s = c->out.front();
    while (s.nextRow < s.nTimeRows) {
      if (ws->getBufferedAmount() > kStreamBackpressure) {
        if (tlc) tlc->m.backpressureWaits++;
        LogStall(ws, s);
        return;   // the rest on drain, once the peer has read some
      }
      uint32_t rows = s.nTimeRows - s.nextRow;
      if (rows > s.chunkRows) rows = s.chunkRows;
      size_t chunkLen = 0;
      eph::writeDataChunk(s.buf.data(), s.buf.size(), s.chunkIndex, s.nextRow,
                          rows, s.nTimeRows, s.nObj, s.precision,
                          s.result->meta.data(), s.result->cols.data(),
                          &chunkLen);
      // The request's line goes out BEFORE its last chunk, so it is in the
      // log by the time a client could have its answer -- a gate reading
      // the log straight after a request never races it.
      if (s.nextRow + rows == s.nTimeRows && !s.fLogged) {
        s.fLogged = true;
        LogRequest(ws, s, s.bytes + eph::kEnvelopeSize + chunkLen);
      }
      // The chunk buffer holds the DATA payload; SendEnvelope wraps it in
      // the 16-byte envelope the protocol requires.
      auto st = SendEnvelope(ws, eph::kMsgData, s.requestId, s.buf.data(),
                             chunkLen);
      if (st == WebSocket<SSL, true, Conn>::SendStatus::DROPPED)
        return;   // not sent: this chunk again on drain
      s.bytes += eph::kEnvelopeSize + chunkLen;
      s.nextRow += rows;
      s.chunkIndex++;
      if (st == WebSocket<SSL, true, Conn>::SendStatus::BACKPRESSURE) {
        if (tlc) tlc->m.backpressureWaits++;
        if (s.nextRow < s.nTimeRows) LogStall(ws, s);
        return;   // sent and buffered: the rest on drain
      }
    }
    c->out.pop_front();
  }
}

// Execute + enqueue one REQUEST (already decoded and limit-checked).
template <bool SSL>
static void RunRequest(WebSocket<SSL, true, Conn> *ws, LoopCtx *lc,
                       const eph::Envelope &env, const eph::Request &req) {
  Conn *c = (Conn *)ws->getUserData();

  Stream s;
  s.tReq = std::chrono::steady_clock::now();
  s.requestId = env.requestId;
  s.precision = req.precision;
  s.nObj = (uint32_t)req.objs.size();
  s.nTimeRows = req.nTime;
  uint32_t hint = req.chunkRows ? req.chunkRows : eph::kMaxChunkRows;
  s.chunkRows = hint > eph::kMaxChunkRows ? eph::kMaxChunkRows : hint;
  if (s.chunkRows == 0) s.chunkRows = 1;

  if (c->out.size() >= kMaxQueuedStreams) {
    SendError(ws, env.requestId, eph::kErrLimits, "too many answers computed "
              "and not yet read on this connection");
    return;
  }
  int32_t errCode = 0;
  std::string errText;
  bool fOk;
  try {
    fOk = ExecuteRequest(lc, req, &s, &errCode, &errText);
  } catch (const std::bad_alloc &) {
    // An exception out of a uWS handler is std::terminate for every loop.
    fOk = false;
    errCode = eph::kErrInternal;
    errText = "out of memory computing this request";
  }
  if (!fOk) {
    SendError(ws, env.requestId, errCode, errText.c_str());
    return;
  }

  s.buf.resize(eph::dataPayloadSize(s.nObj, (size_t)s.chunkRows, s.precision));
  uint32_t nObj = s.nObj, nRows = s.nTimeRows;
  s.fHit = lc->lastWasHit;
  s.computeMs = lc->lastComputeMs;
  if (gOpt.logContents) s.contents = RequestContents(req);
  c->reqs++;
  c->cells += (uint64_t)nObj * nRows;
  if (s.fHit) c->hits++;
  lc->m.requests++;
  if (lc->lastWasHit) {
    lc->m.cacheHits++;
  } else {
    lc->m.cacheMisses++;
    lc->m.cells += (uint64_t)nObj * nRows;
    lc->m.computeCount++;
    lc->m.computeMicros += (uint64_t)(lc->lastComputeMs * 1000.0);
    for (int b = 0; b < kComputeBuckets; b++)
      if (lc->lastComputeMs <= kComputeBucketsMs[b]) lc->m.computeBucket[b]++;
  }
  c->out.push_back(std::move(s));
  FlushStreams(ws);
}

// Handle one decoded WebSocket message. The envelope is checked for size
// agreement first; a payload-length mismatch is a protocol error, not a
// truncation to recover from. Every HELLO is answered with WELCOME. From
// protocol 3 a REQUEST before HELLO is refused and the connection closed:
// HELLO fixes the session's version, which every later message is written
// in, and carries the token a server may require.
template <bool SSL>
static void HandleMessage(WebSocket<SSL, true, Conn> *ws,
                          std::string_view message, LoopCtx *lc) {
  if (message.size() < eph::kEnvelopeSize) {
    SendError(ws, 0, eph::kErrBad, "message shorter than the envelope");
    return;
  }
  eph::Envelope env;
  if (!eph::parseEnvelope((const uint8_t *)message.data(), &env)) {
    uint8_t vOld;
    if (eph::envelopeVersionBelowMin((const uint8_t *)message.data(), &vOld)) {
      // Too old to talk to, and too old to read a newer envelope: the
      // refusal goes out in the client's own version, then the close.
      Conn *c = (Conn *)ws->getUserData();
      c->proto = vOld;
      char sz[160];
      snprintf(sz, sizeof(sz), "this client speaks protocol %u; this server "
               "needs %u to %u -- update Astrolog", (unsigned)vOld,
               (unsigned)eph::kProtoMin, (unsigned)eph::kProtoVersion);
      LogEvt(kLogWarn, "hello.refuse").Conn(c).S("reason", "version")
        .U("client_proto", vOld);
      SendError(ws, 0, eph::kErrVersion, sz);
      ws->end(1008, "protocol too old");
      return;
    }
    SendError(ws, 0, eph::kErrBad, "bad magic or protocol version");
    return;
  }
  if (env.flags & ~eph::kEnvFlagMask) {
    SendError(ws, env.requestId, eph::kErrBad, "unknown envelope flag bits");
    return;
  }
  if (env.flags & eph::kEnvFlagZstd) {
    // Advertised by no WELCOME caps bit; parsing compressed bytes as raw
    // would answer a question nobody asked.
    SendError(ws, env.requestId, eph::kErrBad, "compressed payloads are "
              "not supported");
    return;
  }
  if (env.payloadLen > eph::kMaxPayload ||
      eph::kEnvelopeSize + (size_t)env.payloadLen != message.size()) {
    SendError(ws, env.requestId, eph::kErrBad, "payload length mismatch");
    return;
  }
  const uint8_t *pl = (const uint8_t *)message.data() + eph::kEnvelopeSize;
  Conn *c = (Conn *)ws->getUserData();

  switch (env.type) {
    case eph::kMsgHello: {
      lc->m.hellos++;
      c->hellos++;
      // Every HELLO is answered: a client that sends a second one is
      // waiting for a WELCOME, and silence would hang it.
      eph::Hello hello;
      if (!eph::parseHello(pl, env.payloadLen, &hello)) {
        SendError(ws, env.requestId, eph::kErrBad, "malformed HELLO");
        return;
      }
      // What the client says it is, capped: it is the client's text.
      c->client = hello.version.substr(0, 80);
      if (hello.protoVersion < eph::kProtoMin) {
        LogEvt(kLogWarn, "hello.refuse").Conn(c).S("reason", "version")
          .U("client_proto", hello.protoVersion).S("client", c->client);
        char sz[160];
        c->proto = env.version;
        snprintf(sz, sizeof(sz), "this client speaks protocol %u; this server "
                 "needs %u to %u -- update Astrolog",
                 (unsigned)hello.protoVersion, (unsigned)eph::kProtoMin,
                 (unsigned)eph::kProtoVersion);
        SendError(ws, env.requestId, eph::kErrVersion, sz);
        ws->end(1008, "protocol too old");
        return;
      }
      // The session speaks the lower of the two ends' highest versions.
      // Fixed by the first HELLO; a later HELLO is answered in it.
      if (c->proto == 0)
        c->proto = (uint8_t)std::min<uint32_t>(hello.protoVersion,
                                               eph::kProtoVersion);
      const char *szToken = hello.token.empty() ? "none" :
                            gTokens.count(hello.token) ? "accepted" : "unknown";
      if (!hello.token.empty() && gTokens.count(hello.token))
        c->budget = "t:" + hello.token;
      else if (gOpt.requireToken) {
        LogEvt(kLogWarn, "hello.refuse").Conn(c).S("reason", "token")
          .S("token", szToken).U("client_proto", hello.protoVersion)
          .S("client", c->client);
        SendError(ws, env.requestId, eph::kErrToken, hello.token.empty() ?
                  "this server requires a token" : "unknown token");
        ws->end(1008, "token refused");
        return;
      }
      {
        static thread_local char szSweVersion[256];
        swe_version(szSweVersion);
        uint8_t wbuf[sizeof(eph::WelcomeWire) + 256];
        uint32_t wlen = 0;
        eph::buildWelcome(wbuf, eph::kCapFloat32,
                          PackSweVersion(szSweVersion), gOpt.maxCells,
                          kServerVersion, &wlen, c->proto);
        SendEnvelope(ws, eph::kMsgWelcome, env.requestId, wbuf, wlen);
      }
      // The first HELLO at info; a repeat, which a client may send, at debug.
      // The token is described, never written.
      LogEvt(c->hellos == 1 ? kLogInfo : kLogDebug, "hello").Conn(c)
        .U("client_proto", hello.protoVersion).U("proto", c->proto)
        .X("caps", hello.caps).U("build", hello.build).S("client", c->client)
        .S("token", szToken)
        .F("after_ms", std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now() - c->tOpen).count(), 1);
      break;
    }
    case eph::kMsgRequest: {
      if (c->proto == 0) {
        LogEvt(kLogWarn, "hello.refuse").Conn(c).S("reason", "request_first");
        SendError(ws, env.requestId, eph::kErrBad, "REQUEST before HELLO");
        ws->end(1008, "HELLO first");
        return;
      }
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
      // The work bound WELCOME advertised (protocol 2, kMaxCellsDefault
      // and --max-cells): a request whose objects x rows exceeds it is
      // refused rather than computed -- 64 bodies x 20000 rows measured
      // 13.7 s on the loop's only thread, and every other connection on
      // that loop waited for it (EPHEMERIS_REVIEW.md S4).
      {
        uint64_t cells = (uint64_t)req.objs.size() * (uint64_t)req.nTime;
        if (cells > gOpt.maxCells) {
          char sz[128];
          snprintf(sz, sizeof(sz), "REQUEST asks %" PRIu64 " cells; WELCOME's"
                   " bound is %u", cells, gOpt.maxCells);
          SendError(ws, env.requestId, eph::kErrLimits, sz);
          return;
        }
        // The compute budget (production plan Phase 3): charged whether the
        // answer is cached or not, since which it will be is not known yet.
        double wait = ChargeCells(c->budget, cells);
        if (wait > 0.0) {
          char sz[160];
          // Rounded UP to the tenth: "0.0 s" for a 12 ms wait read as "now".
          snprintf(sz, sizeof(sz), "rate limited: %u cells a second; ask "
                   "again in %.1f s", gOpt.cellsPerSec,
                   std::ceil(wait * 10.0) / 10.0);
          SendError(ws, env.requestId, eph::kErrRateLimited, sz);
          return;
        }
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


template <bool SSL>
static void OnMessage(WebSocket<SSL, true, Conn> *ws, std::string_view message,
                      uWS::OpCode op) {
  if (op != uWS::OpCode::BINARY) {
    SendError(ws, 0, eph::kErrBad, "the protocol is binary frames only");
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
static std::atomic<bool> gDraining{false};

static std::string MetricsText() {
  uint64_t connOpen = 0, connTotal = 0, hellos = 0, requests = 0, cells = 0,
           hits = 0, misses = 0, bytes = 0, bp = 0, cCompute = 0, uCompute = 0;
  uint64_t errors[eph::kErrMax + 1] = {0}, buckets[kComputeBuckets] = {0};
  uint64_t refused = 0, helloTimeouts = 0;
  for (LoopCtx &lc : *gLoops) {
    connOpen += lc.m.connOpen; connTotal += lc.m.connTotal;
    hellos += lc.m.hellos; requests += lc.m.requests; cells += lc.m.cells;
    hits += lc.m.cacheHits; misses += lc.m.cacheMisses;
    bytes += lc.m.bytesSent; bp += lc.m.backpressureWaits;
    cCompute += lc.m.computeCount; uCompute += lc.m.computeMicros;
    for (int i = 0; i <= eph::kErrMax; i++) errors[i] += lc.m.errors[i];
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
  line("Bytes of WebSocket messages sent.", "counter", "ephd_bytes_sent_total", bytes);
  line("Times a stream waited for the client to read.", "counter",
       "ephd_backpressure_waits_total", bp);
  out += "# HELP ephd_errors_total ERRORs sent, by protocol code (0 other).\n"
         "# TYPE ephd_errors_total counter\n";
  for (int i = 0; i <= eph::kErrMax; i++) {
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
  behavior.maxPayloadLength = (unsigned)(eph::kMaxPayload + 65536);
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
  if (disc.resolved.empty())
    LogEvt(kLogWarn, "ephe").S("path", "")
      .Msg("no ephemeris directory found; file-backed requests fail");
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
