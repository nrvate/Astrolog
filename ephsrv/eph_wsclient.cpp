// eph_wsclient: a standalone single-connection WebSocket client for the
// ephemeris server, speaking protocol version 4 (ephsrv/ephproto.h;
// EPHEMERIS_PLUGINS_PLAN.md section 3). The server's gates drive it:
// tools/ephsrv-{golden,soak,cache,bench,robust,tls,ops,limits,load,image}.sh.
// Raw sockets: uWS has no client, so the WebSocket framing is by hand.
//
//   connection  --host H --port P [--tls [--ca FILE] [--sni NAME]]
//               [--token T] [--proto N] [--proto-min N] [--quiet]
//   time        [--jd JD] [--jd2 X] [--step SEC | --step-ns NS] [--count N]
//               [--list JD,JD,...] [--ut] [--deltat SEC]
//               [--deltat-table JD:SEC,JD:SEC,...]
//   delivery    [--precision 64|32] [--chunk-rows N] [--priority LIST]
//   profiles    [--profile SPEC]...   each starts a profile the objects named
//               after it use; SPEC is key=value[,key=value...] of
//                 obs=geo|topo|helio|bary|body:NAIF  site=LON:LAT:HEIGHT
//                 plane=ecl|equ  form=sph|rect  frame=tod|mod|j2000|icrf
//                 corr=MASK  speeds=0|1  zodiac=TOKEN  sidplane=0|1|2
//                 anchor=JD:AYANAMSA  cols=MASK
//   objects     [--objs NAIF,...] [--points NAIF:POINT:METHOD,...]
//               [--stars NAME,...] [--hypo TOKEN,...] [--desig TEXT;...]
//               [--elements NAME]
//   output      [--out FILE] [--meta] [--expect-rows N]
//   runs        [--repeat N] [--latency FILE] [--sleep-ms MS]
//               [--burst N [--burst-step DAYS]] [--cycles N] [--hello N]
//               [--no-hello] [--idle-ms MS] [--cancel-after-ms MS]
//   other       [--lookup QUERY ... [--lookup-flags F] [--max-matches N]]
//               [--deadline-ms MS]
//               [--send-hex FILE] [--pin DATASET]
//
// Sends HELLO, prints WELCOME unless --quiet, sends one REQUEST, collects its
// DATA chunks -- every one checked: rows exactly once, chunks in order, the
// header agreeing with the request, metadata on chunk 0 -- and with --out
// writes one line per object per row:
//   <objIdx> <label> <errCode> <rowsOk> <metaFlags> <col0> ... <colN>
// columns in hexfloat, the six base columns then the extra ones present. The
// label is a body's NAIF id, o:NAIF/POINT/METHOD, s:NAME, h:TOKEN, d:TEXT or
// e:NAME (spaces as '_'). --meta prints each object's META on stdout.
//
// Exit 0 on success; a server ERROR prints "server ERROR <code> (request N):
// text" (or "(HELLO, vN)") to stderr and exits 2; a TLS handshake failure
// exits 3 with OpenSSL's reason, so a gate can tell it from a refused
// connection (2).
//
// --proto N below 4 sends an older client's HELLO in version N, which the
// server must refuse with ERROR 8 in that version's own layout; above 4 it
// is a newer client (protoMax N) that must be welcomed in 4. --proto-min N
// asks for a floor the server cannot meet. --repeat re-runs the request on
// the same connection; --latency appends each run's microseconds from
// REQUEST to last chunk. --sleep-ms reads nothing for MS after sending, so
// the answer backs up; --burst N sends N requests at once (instants
// --burst-step days apart, default 1; 0 makes them one cached answer) and
// prints how many were answered and how many refused ERROR 9 (busy);
// --cycles N only connects, is welcomed and closes, N times; --hello N sends
// HELLO N times and requires a WELCOME for each; --no-hello skips HELLO
// (after --idle-ms if given). --cancel-after-ms MS cancels the request MS
// after sending it and requires ERROR 10, then a PONG with no DATA for the
// cancelled request in between. --lookup sends a LOOKUP and prints its
// matches; given more than once it batches the queries into ONE message, and
// --max-matches is then the budget for the whole answer (3.4). --deadline-ms
// sets the REQUEST's advisory deadline, which a server may use to choose a
// cheaper strategy and must never fail the request for. --send-hex sends one
// frame from a hex file (a conformance fixture) and prints what came back.
// --pin adds an ephemeris pin (A.4).

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cctype>
#include <cstdarg>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "ephproto.h"

using namespace eph;

// Bytes over-read by the handshake reader; drained by readFull.
static std::vector<uint8_t> gPending;

// The TLS session when --tls, else null and every read and write is the
// plain socket's. One connection at a time, so one global is enough.
static SSL_CTX *gSslCtx = nullptr;
static SSL *gSsl = nullptr;

static ssize_t ioRecv(int fd, void *buf, size_t n) {
  if (gSsl == nullptr) return recv(fd, buf, n, 0);
  int r = SSL_read(gSsl, buf, n > 0x7FFFFFFF ? 0x7FFFFFFF : (int)n);
  return r > 0 ? r : -1;
}

static ssize_t ioSend(int fd, const void *buf, size_t n) {
  if (gSsl == nullptr) return send(fd, buf, n, 0);
  int r = SSL_write(gSsl, buf, n > 0x7FFFFFFF ? 0x7FFFFFFF : (int)n);
  return r > 0 ? r : -1;
}

static void closeConn(int fd) {
  if (gSsl != nullptr) {
    SSL_shutdown(gSsl);
    SSL_free(gSsl);
    gSsl = nullptr;
  }
  close(fd);
}

static int readFull(int fd, uint8_t *buf, size_t n) {
  size_t got = 0;
  // The handshake reader may have over-read past the HTTP header end -- TCP
  // happily coalesces the 101 response with the first WebSocket frame -- so
  // bytes stashed in gPending drain before the socket does.
  while (got < n && !gPending.empty()) {
    size_t take = n - got < gPending.size() ? n - got : gPending.size();
    memcpy(buf + got, gPending.data(), take);
    gPending.erase(gPending.begin(), gPending.begin() + (long)take);
    got += take;
  }
  while (got < n) {
    ssize_t r = ioRecv(fd, buf + got, n - got);
    if (r <= 0) return -1;
    got += (size_t)r;
  }
  return 0;
}

// Read one complete WebSocket message (de-fragmented). Handles the server's
// protocol pings (answering them, since uWS closes on missed pongs), close
// frames, and interleaved control frames. Returns 0 on success.
enum { kGotMessage = 0, kGotClose = 1 };

static int readWsMessage(int fd, std::vector<uint8_t> *out) {
  out->clear();
  int opStart = -1;
  for (;;) {
    uint8_t hdr[2];
    if (readFull(fd, hdr, 2) != 0) return -1;
    bool fin = hdr[0] & 0x80;
    int op = hdr[0] & 0x0F;
    bool masked = hdr[1] & 0x80;
    (void)masked;   // server frames are never masked
    uint64_t len = hdr[1] & 0x7F;
    if (len == 126) {
      uint8_t e[2];
      if (readFull(fd, e, 2) != 0) return -1;
      len = ((uint64_t)e[0] << 8) | e[1];        // WS lengths are big-endian
    } else if (len == 127) {
      uint8_t e[8];
      if (readFull(fd, e, 8) != 0) return -1;
      len = 0;
      for (int i = 0; i < 8; i++) len = (len << 8) | e[i];
    }
    if (len > ((uint64_t)64 << 20)) return -1;   // no answer is this big
    std::vector<uint8_t> pay((size_t)len);
    if (len && readFull(fd, pay.data(), (size_t)len) != 0) return -1;
    if (op == 9) {   // ping: mask + send pong, keep reading
      uint8_t pong[125];
      size_t n = pay.size() > 125 ? 125 : pay.size();
      for (size_t i = 0; i < n; i++) pong[i] = pay[i];
      for (size_t i = 0; i < n; i++) pong[i] ^= 0;   // placeholder masked below
      uint8_t frame[132];
      size_t f = 0;
      frame[f++] = 0x8A;   // FIN + pong
      if (n < 126) {
        frame[f++] = (uint8_t)(0x80 | n);
      } else {
        frame[f++] = 0x80 | 126;
        frame[f++] = (uint8_t)(n >> 8); frame[f++] = (uint8_t)n;
      }
      uint8_t mask[4] = {0x2A, 0x4B, 0x17, 0x99};
      memcpy(frame + f, mask, 4); f += 4;
      for (size_t i = 0; i < n; i++) frame[f + i] = pong[i] ^ mask[i & 3];
      if (ioSend(fd, frame, f + n) < 0) return -1;
      continue;
    }
    if (op == 10) continue;   // pong
    if (op == 8) return kGotClose;
    if (op == 2 || op == 1 || op == 0) {
      if (op != 0 && out->empty()) opStart = op;
      out->insert(out->end(), pay.begin(), pay.end());
      if (fin) {
        (void)opStart;
        return kGotMessage;
      }
      continue;   // fragment; wait for continuation frames
    }
    return -1;   // unexpected opcode
  }
}

static void sendWsBinary(int fd, const std::vector<uint8_t> &msg) {
  size_t len = msg.size();
  std::vector<uint8_t> frame(len + 16);
  size_t f = 0;
  frame[f++] = 0x82;   // FIN + binary
  if (len < 126) {
    frame[f++] = (uint8_t)(0x80 | len);
  } else if (len < 65536) {
    frame[f++] = 0x80 | 126;
    frame[f++] = (uint8_t)(len >> 8); frame[f++] = (uint8_t)len;
  } else {
    frame[f++] = 0x80 | 127;
    for (int i = 7; i >= 0; i--) frame[f++] = (uint8_t)(len >> (8 * i));
  }
  uint8_t mask[4] = {0x1B, 0x77, 0x3E, 0xC2};
  memcpy(frame.data() + f, mask, 4); f += 4;
  for (size_t i = 0; i < len; i++) frame[f + i] = msg[i] ^ mask[i & 3];
  size_t sent = 0;
  while (sent < f + len) {
    ssize_t r = ioSend(fd, frame.data() + sent, f + len - sent);
    if (r <= 0) { fprintf(stderr, "wsclient: send failed\n"); exit(2); }
    sent += (size_t)r;
  }
}

// A TLS handshake failure is not a connection failure: exit 3 with the
// reason, so a gate expecting a refused certificate can require exactly it.
static void tlsFailed(const char *what) {
  char sz[256];
  unsigned long e = ERR_get_error();
  ERR_error_string_n(e, sz, sizeof(sz));
  long v = gSsl ? SSL_get_verify_result(gSsl) : X509_V_OK;
  fprintf(stderr, "wsclient: TLS %s failed: %s%s%s\n", what, e ? sz : "",
          v != X509_V_OK ? "; certificate: " : "",
          v != X509_V_OK ? X509_verify_cert_error_string(v) : "");
  exit(3);
}

static bool connectWs(const char *host, uint16_t port, int *fdOut,
                      const char *sni) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return false;
  sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, host, &addr.sin_addr) != 1)
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(fd, (sockaddr *)&addr, sizeof(addr)) != 0) { close(fd); return false; }
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  timeval tv {};
  tv.tv_sec = 90;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  if (gSslCtx != nullptr) {
    const char *name = sni ? sni : host;
    gSsl = SSL_new(gSslCtx);
    SSL_set_fd(gSsl, fd);
    SSL_set_tlsext_host_name(gSsl, name);
    // Name checking: an IP literal matches the certificate's IP entries,
    // anything else its DNS names.
    in_addr ip4 {};
    if (inet_pton(AF_INET, name, &ip4) == 1)
      X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(gSsl), name);
    else
      SSL_set1_host(gSsl, name);
    if (SSL_connect(gSsl) != 1)
      tlsFailed("handshake");
  }

  char hs[256];
  snprintf(hs, sizeof(hs),
    "GET / HTTP/1.1\r\n"
    "Host: %s:%u\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
    "Sec-WebSocket-Version: 13\r\n\r\n", host, (unsigned)port);
  if (ioSend(fd, hs, strlen(hs)) < 0) { closeConn(fd); return false; }
  std::string acc;
  while (acc.find("\r\n\r\n") == std::string::npos) {
    char tmp[1024];
    ssize_t r = ioRecv(fd, tmp, sizeof(tmp));
    if (r <= 0) { closeConn(fd); return false; }
    acc.append(tmp, (size_t)r);
    if (acc.size() > 65536) { closeConn(fd); return false; }
  }
  if (acc.find(" 101 ") == std::string::npos) { closeConn(fd); return false; }
  size_t hdrEnd = acc.find("\r\n\r\n");
  if (hdrEnd + 4 < acc.size())
    gPending.assign(acc.begin() + (long)(hdrEnd + 4), acc.end());
  *fdOut = fd;
  return true;
}

static void die(const char *fmt, ...) __attribute__((format(printf, 1, 2), noreturn));
static void die(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "wsclient: ");
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
  exit(2);
}

static void sendMessage(int fd, uint16_t type, uint32_t requestId, const std::vector<uint8_t> &pay,
                        uint8_t version = kProtoVersion) {
  std::vector<uint8_t> msg;
  WriteEnvelope(&msg, type, requestId, pay.size(), version);
  msg.insert(msg.end(), pay.begin(), pay.end());
  sendWsBinary(fd, msg);
}

// One message, envelope parsed. Returns false on a close or a dead socket.
static bool readMessage(int fd, std::vector<uint8_t> *msg, Envelope *env) {
  if (readWsMessage(fd, msg) != kGotMessage) return false;
  std::string why;
  if (ParseEnvelope(msg->data(), msg->size(), env, &why) != kOk)
    die("bad envelope on a message of %zu bytes: %s", msg->size(), why.c_str());
  return true;
}

// An ERROR, in whichever layout its envelope version says, printed as the
// gates read it. Returns the code.
static int printError(const std::vector<uint8_t> &msg, const Envelope &env, const char *what) {
  const uint8_t *pl = msg.data() + kEnvelopeSize;
  std::string why;
  if (env.version < kProtoMin) {
    LegacyError le;
    if (ParseLegacyError(pl, env.payloadLen, &le, &why) != kOk) die("malformed legacy ERROR: %s", why.c_str());
    fprintf(stderr, "wsclient: server ERROR %d (%s, v%u): %s\n", le.code, what, (unsigned)env.version,
            le.text.c_str());
    return le.code;
  }
  Error e;
  if (ParseError(pl, env.payloadLen, &e, &why) != kOk) die("malformed ERROR: %s", why.c_str());
  if (strcmp(what, "HELLO") == 0)
    fprintf(stderr, "wsclient: server ERROR %u (HELLO, v%u)%s%s: %s\n", e.code, (unsigned)env.version,
            e.flags & kErrFlagClosing ? " closing" : "", e.flags & kErrFlagRetryable ? " retryable" : "",
            e.text.c_str());
  else
    fprintf(stderr, "wsclient: server ERROR %u (request %u)%s%s retry_ms=%u: %s\n", e.code, env.requestId,
            e.flags & kErrFlagClosing ? " closing" : "", e.flags & kErrFlagRetryable ? " retryable" : "",
            e.retryAfterMs, e.text.c_str());
  return e.code;
}

static std::vector<std::string> split(const char *sz, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (const char *p = sz; ; p++) {
    if (*p == sep || *p == '\0') {
      if (!cur.empty()) out.push_back(cur);
      cur.clear();
      if (*p == '\0') break;
    } else {
      cur += *p;
    }
  }
  return out;
}

static Profile parseProfile(const char *spec) {
  Profile pf;
  for (const std::string &kv : split(spec, ',')) {
    size_t eq = kv.find('=');
    if (eq == std::string::npos) die("bad --profile entry %s", kv.c_str());
    std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
    if (k == "obs") {
      if (v == "geo") pf.observer = kObsGeo;
      else if (v == "topo") pf.observer = kObsTopo;
      else if (v == "helio") pf.observer = kObsHelio;
      else if (v == "bary") pf.observer = kObsBary;
      else if (v.compare(0, 5, "body:") == 0) { pf.observer = kObsBody; pf.observerBody = atoi(v.c_str() + 5); }
      else pf.observer = (uint8_t)atoi(v.c_str());
    } else if (k == "site") {
      if (sscanf(v.c_str(), "%lf:%lf:%lf", &pf.siteLonEastDeg, &pf.siteLatDeg, &pf.siteHeightM) != 3)
        die("bad site %s", v.c_str());
    } else if (k == "plane") {
      pf.plane = v == "equ" ? (uint8_t)kPlaneEquator : v == "ecl" ? (uint8_t)kPlaneEcliptic : (uint8_t)atoi(v.c_str());
    } else if (k == "form") {
      pf.form = v == "rect" ? (uint8_t)kFormRectangular : v == "sph" ? (uint8_t)kFormSpherical : (uint8_t)atoi(v.c_str());
    } else if (k == "frame") {
      pf.frame = v == "tod" ? (uint8_t)kFrameTrueOfDate : v == "mod" ? (uint8_t)kFrameMeanOfDate :
                 v == "j2000" ? (uint8_t)kFrameJ2000 : v == "icrf" ? (uint8_t)kFrameIcrf : (uint8_t)atoi(v.c_str());
    } else if (k == "corr") {
      pf.corrections = (uint8_t)atoi(v.c_str());
    } else if (k == "speeds") {
      pf.speeds = (uint8_t)atoi(v.c_str());
    } else if (k == "zodiac") {
      pf.zodiac = v;
    } else if (k == "sidplane") {
      pf.siderealPlane = (uint8_t)atoi(v.c_str());
    } else if (k == "anchor") {
      if (sscanf(v.c_str(), "%lf:%lf", &pf.anchorEpoch.jd1, &pf.anchorAyanamsaDeg) != 2)
        die("bad anchor %s", v.c_str());
    } else if (k == "cols") {
      pf.columns = (uint32_t)strtoul(v.c_str(), nullptr, 0);
    } else {
      die("unknown --profile key %s", k.c_str());
    }
  }
  return pf;
}

static std::string labelOf(const Object &o) {
  std::string s;
  switch (o.kind) {
    case kObjBody: s = std::to_string(o.naif); break;
    case kObjOrbitPoint:
      s = "o:" + std::to_string(o.naif) + "/" + std::to_string(o.point) + "/" + std::to_string(o.method);
      break;
    case kObjStar: s = "s:" + o.name; break;
    case kObjHypothetical: s = "h:" + o.name; break;
    case kObjDesignation: s = "d:" + o.name; break;
    default: s = "e:" + o.name; break;
  }
  for (char &ch : s)
    if (ch == ' ') ch = '_';
  return s;
}

static bool readHexFile(const char *path, std::vector<uint8_t> *out) {
  FILE *f = fopen(path, "r");
  if (!f) return false;
  std::string hex;
  int ch;
  while ((ch = fgetc(f)) != EOF)
    if (isxdigit(ch)) hex += (char)ch;
  fclose(f);
  if (hex.size() % 2) return false;
  out->clear();
  for (size_t i = 0; i < hex.size(); i += 2)
    out->push_back((uint8_t)strtoul(hex.substr(i, 2).c_str(), nullptr, 16));
  return true;
}

int main(int argc, char **argv) {
  const char *host = "127.0.0.1", *outFile = nullptr, *latencyFile = nullptr;
  const char *szCa = nullptr, *szSni = nullptr, *szToken = nullptr;
  std::vector<std::string> lookups;   // --lookup, repeatable: one message
  const char *szSendHex = nullptr, *szPin = nullptr, *szList = nullptr;
  const char *szDeltaTTable = nullptr, *szPriority = nullptr;
  bool fTls = false, quiet = false, fMeta = false, fUT = false, fNoHello = false;
  uint16_t port = kDefaultPort;
  int protoVersion = kProtoVersion, protoMin = -1;
  double jd = 2451545.0, jd2 = 0.0, deltaT = CanonicalNaN(), burstStep = 1.0;
  int64_t stepNs = 600LL * 1000000000LL;
  uint32_t count = 5, repeat = 1, expectRows = 0, chunkRows = 500;
  uint32_t sleepMs = 0, burst = 0, cycles = 0, nHello = 1, idleMs = 0, cancelAfterMs = 0;
  int precision = 64, lookupFlags = 0, maxMatches = 8, deadlineMs = 0;
  Request req;
  req.profiles.push_back(Profile());
  bool fProfileUsed = false, fFirstProfile = true;
  auto addObj = [&](Object o) {
    o.profile = (uint8_t)(req.profiles.size() - 1);
    fProfileUsed = true;
    req.objs.push_back(std::move(o));
  };

  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    const char *v = nullptr;
    auto next = [&]() -> bool { if (i + 1 < argc) { v = argv[++i]; return true; } return false; };
    if (!strcmp(a, "--host") && next()) host = v;
    else if (!strcmp(a, "--port") && next()) port = (uint16_t)atoi(v);
    else if (!strcmp(a, "--out") && next()) outFile = v;
    else if (!strcmp(a, "--jd") && next()) jd = atof(v);
    else if (!strcmp(a, "--jd2") && next()) jd2 = atof(v);
    else if (!strcmp(a, "--step") && next()) stepNs = (int64_t)(atof(v) * 1e9);
    else if (!strcmp(a, "--step-ns") && next()) stepNs = strtoll(v, nullptr, 10);
    else if (!strcmp(a, "--count") && next()) count = (uint32_t)strtoul(v, nullptr, 10);
    else if (!strcmp(a, "--list") && next()) szList = v;
    else if (!strcmp(a, "--ut")) fUT = true;
    else if (!strcmp(a, "--deltat") && next()) deltaT = atof(v);
    else if (!strcmp(a, "--deltat-table") && next()) szDeltaTTable = v;
    else if (!strcmp(a, "--priority") && next()) szPriority = v;
    else if (!strcmp(a, "--precision") && next()) precision = atoi(v);
    else if (!strcmp(a, "--chunk-rows") && next()) chunkRows = (uint32_t)strtoul(v, nullptr, 10);
    else if (!strcmp(a, "--profile") && next()) {
      Profile pf = parseProfile(v);
      if (fFirstProfile && !fProfileUsed) req.profiles[0] = pf;
      else req.profiles.push_back(pf);
      fFirstProfile = false;
    } else if (!strcmp(a, "--objs") && next()) {
      for (const std::string &t : split(v, ',')) {
        Object o; o.kind = kObjBody; o.naif = atoi(t.c_str()); addObj(o);
      }
    } else if (!strcmp(a, "--points") && next()) {
      for (const std::string &t : split(v, ',')) {
        Object o;
        int naif, pt, meth;
        if (sscanf(t.c_str(), "%d:%d:%d", &naif, &pt, &meth) != 3) die("bad --points entry %s", t.c_str());
        o.kind = kObjOrbitPoint; o.naif = naif; o.point = (uint8_t)pt; o.method = (uint8_t)meth;
        addObj(o);
      }
    } else if (!strcmp(a, "--stars") && next()) {
      for (const std::string &t : split(v, ',')) { Object o; o.kind = kObjStar; o.name = t; addObj(o); }
    } else if (!strcmp(a, "--hypo") && next()) {
      for (const std::string &t : split(v, ',')) { Object o; o.kind = kObjHypothetical; o.name = t; addObj(o); }
    } else if (!strcmp(a, "--desig") && next()) {
      for (const std::string &t : split(v, ';')) { Object o; o.kind = kObjDesignation; o.name = t; addObj(o); }
    } else if (!strcmp(a, "--elements") && next()) {
      Object o;
      o.kind = kObjElements; o.name = v; o.epoch = {2451545.0, 0.0}; o.nTerms = 1;
      o.coef = {10.0, 2.0, 0.1, 30.0, 40.0, 5.0};
      addObj(o);
    }
    else if (!strcmp(a, "--expect-rows") && next()) expectRows = (uint32_t)strtoul(v, nullptr, 10);
    else if (!strcmp(a, "--repeat") && next()) repeat = (uint32_t)strtoul(v, nullptr, 10);
    else if (!strcmp(a, "--latency") && next()) latencyFile = v;
    else if (!strcmp(a, "--meta")) fMeta = true;
    else if (!strcmp(a, "--quiet")) quiet = true;
    else if (!strcmp(a, "--sleep-ms") && next()) sleepMs = (uint32_t)strtoul(v, nullptr, 10);
    else if (!strcmp(a, "--burst") && next()) burst = (uint32_t)strtoul(v, nullptr, 10);
    else if (!strcmp(a, "--burst-step") && next()) burstStep = atof(v);
    else if (!strcmp(a, "--cycles") && next()) cycles = (uint32_t)strtoul(v, nullptr, 10);
    else if (!strcmp(a, "--hello") && next()) nHello = (uint32_t)strtoul(v, nullptr, 10);
    else if (!strcmp(a, "--no-hello")) fNoHello = true;
    else if (!strcmp(a, "--idle-ms") && next()) idleMs = (uint32_t)strtoul(v, nullptr, 10);
    else if (!strcmp(a, "--cancel-after-ms") && next()) cancelAfterMs = (uint32_t)strtoul(v, nullptr, 10);
    else if (!strcmp(a, "--lookup") && next()) lookups.push_back(v);
    else if (!strcmp(a, "--lookup-flags") && next()) lookupFlags = atoi(v);
    else if (!strcmp(a, "--max-matches") && next()) maxMatches = atoi(v);
    else if (!strcmp(a, "--deadline-ms") && next()) deadlineMs = atoi(v);
    else if (!strcmp(a, "--send-hex") && next()) szSendHex = v;
    else if (!strcmp(a, "--pin") && next()) szPin = v;
    else if (!strcmp(a, "--tls")) fTls = true;
    else if (!strcmp(a, "--ca") && next()) szCa = v;
    else if (!strcmp(a, "--sni") && next()) szSni = v;
    else if (!strcmp(a, "--proto") && next()) protoVersion = atoi(v);
    else if (!strcmp(a, "--proto-min") && next()) protoMin = atoi(v);
    else if (!strcmp(a, "--token") && next()) szToken = v;
    else { fprintf(stderr, "wsclient: unknown/incomplete option %s\n", a); return 1; }
  }

  // The question: the time block, then whatever objects and profiles the
  // options built. A profile no object used is dropped from the end.
  while (req.profiles.size() > 1) {
    bool fUsed = false;
    for (const Object &o : req.objs) fUsed = fUsed || o.profile == req.profiles.size() - 1;
    if (fUsed) break;
    req.profiles.pop_back();
  }
  req.precision = precision == 32 ? kPrecF32 : kPrecF64;
  req.chunkRows = chunkRows;
  req.deadlineMs = (uint32_t)deadlineMs;
  req.timeScale = fUT ? kTimeUT1 : kTimeTT;
  req.deltaTSec = deltaT;
  if (szList) {
    req.timeMode = kTimeList;
    for (const std::string &t : split(szList, ',')) req.instants.push_back({atof(t.c_str()), 0.0});
    req.nTime = (uint32_t)req.instants.size();
    count = req.nTime;
  } else {
    req.timeMode = kTimeGrid;
    req.start = {jd, jd2};
    req.nTime = count;
    req.stepNs = count > 1 ? stepNs : 0;
  }
  if (szDeltaTTable) {
    // A.4 0x8004: the client's own delta T, TT instants ascending. The
    // request's own deltaTSec must then be the canonical NaN.
    std::vector<std::pair<Time, double>> tab;
    for (const std::string &t : split(szDeltaTTable, ',')) {
      double jdT, dt;
      if (sscanf(t.c_str(), "%lf:%lf", &jdT, &dt) != 2) die("bad --deltat-table entry %s", t.c_str());
      tab.push_back({{jdT, 0.0}, dt});
    }
    Tlv e;
    EncodeDeltaTTable(tab, &e);
    req.ext.push_back(e);
    req.deltaTSec = CanonicalNaN();
  }
  if (szPin) {
    Tlv e;
    e.tag = kReqTagEphemerisPin;
    e.value.push_back((char)(uint8_t)strlen(szPin));
    e.value += szPin;
    req.ext.push_back(e);
  }
  if (req.objs.empty() && lookups.empty() && !szSendHex && cycles == 0)
    die("no objects: --objs, --points, --stars, --hypo, --desig or --elements");

  if (fTls) {
    gSslCtx = SSL_CTX_new(TLS_client_method());
    SSL_CTX_set_min_proto_version(gSslCtx, TLS1_2_VERSION);
    SSL_CTX_set_verify(gSslCtx, SSL_VERIFY_PEER, nullptr);
    if (szCa ? SSL_CTX_load_verify_locations(gSslCtx, szCa, nullptr) != 1
             : SSL_CTX_set_default_verify_paths(gSslCtx) != 1)
      tlsFailed("loading the CA");
  }

  int fd = -1;
  for (uint32_t cyc = 0; cyc <= cycles; cyc++) {
    if (!connectWs(host, port, &fd, szSni)) {
      fprintf(stderr, "wsclient: cannot connect to %s:%u\n", host, (unsigned)port);
      return 2;
    }
    if (idleMs) usleep(idleMs * 1000);
    for (uint32_t h = 0; !fNoHello && h < (nHello ? nHello : 1); h++) {
      std::vector<uint8_t> pay;
      if (protoVersion < kProtoMin) {
        // An older client's HELLO, in its own layout and version: the
        // version, caps and build as u32, then its name NUL-terminated.
        Writer w(&pay);
        w.u32((uint32_t)protoVersion); w.u32(0); w.u32(0);
        w.raw("eph_wsclient/2.0", 17);
        sendMessage(fd, kMsgHello, 0, pay, (uint8_t)protoVersion);
      } else {
        Hello hello;
        hello.protoMax = (uint32_t)protoVersion;
        hello.protoMin = protoMin >= 0 ? (uint32_t)protoMin : kProtoMin;
        hello.caps = kCapF32 | kCapCancel | kCapLookup | kCapInstantLists | kCapDesignations;
        hello.clientName = "eph_wsclient/2.0";
        if (szToken) hello.token = szToken;
        EncodeHello(&pay, hello);
        sendMessage(fd, kMsgHello, 0, pay, (uint8_t)protoVersion);
      }
      std::vector<uint8_t> msg;
      Envelope env;
      if (!readMessage(fd, &msg, &env)) die("no WELCOME: the connection closed");
      if (env.type == kMsgError) {
        printError(msg, env, "HELLO");
        return 2;
      }
      if (env.type != kMsgWelcome) die("expected WELCOME, got type %u", env.type);
      if (env.version != kProtoVersion) die("WELCOME in version %u", (unsigned)env.version);
      Welcome w;
      Capabilities caps;
      std::string why;
      if (ParseWelcome(msg.data() + kEnvelopeSize, env.payloadLen, &w, &why) != kOk)
        die("malformed WELCOME: %s", why.c_str());
      if (ParseCapabilities(w.caps_, &caps, &why) != kOk) die("WELCOME capabilities: %s", why.c_str());
      if (!quiet)
        printf("WELCOME v%u caps=0x%x maxObjs=%u maxRows=%u maxChunk=%u maxCells=%u maxProfiles=%u "
               "kinds=0x%x observers=0x%x masks=%zu columns=0x%x zodiacs=%zu scales=0x%x "
               "lookup=%u hypotheticals=%zu rate=%u deltat=%s server=%s dataset=%s engine=\"%s\"\n",
               w.protoSession, w.caps, w.maxObjs, w.maxRows, w.maxChunkRows, w.maxCells,
               (unsigned)w.maxProfiles, caps.kinds, caps.observers, caps.corrMasks.size(), caps.columns,
               caps.zodiacs.size(), caps.timeScales, (unsigned)caps.lookupMax, caps.hypotheticals.size(),
               caps.fRate ? caps.cellsPerSec : 0, caps.deltaTModel.c_str(), w.serverName.c_str(),
               w.datasetId.c_str(), w.engine.c_str());
    }
    if (cyc < cycles) {
      closeConn(fd);
      gPending.clear();
      if (cyc + 1 == cycles) return 0;   // --cycles: connections only
    }
  }

  if (szSendHex) {
    std::vector<uint8_t> frame, msg;
    if (!readHexFile(szSendHex, &frame)) die("cannot read %s", szSendHex);
    sendWsBinary(fd, frame);
    // A frame the server answers nothing to -- a CANCEL for a request it
    // does not have, a PONG -- is a legitimate outcome, so this read is
    // bounded rather than left on the 90 s socket timeout.
    {
      timeval tv {};
      tv.tv_sec = 2;
      setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
    Envelope env;
    if (!readMessage(fd, &msg, &env)) {
      printf("no answer\n");
      return 0;
    }
    printf("ANSWER type=%u id=%u version=%u\n", env.type, env.requestId, (unsigned)env.version);
    if (env.type == kMsgError) printError(msg, env, "sent");
    return 0;
  }

  if (!lookups.empty()) {
    Lookup l;
    l.maxMatches = (uint16_t)maxMatches;
    l.flags = (uint8_t)lookupFlags;
    l.queries = lookups;
    std::vector<uint8_t> pay, msg;
    EncodeLookup(&pay, l);
    sendMessage(fd, kMsgLookup, 1, pay);
    Envelope env;
    if (!readMessage(fd, &msg, &env)) die("connection closed awaiting LOOKUP_RESULT");
    if (env.type == kMsgError) {
      printError(msg, env, "lookup");
      return 2;
    }
    LookupResult lr;
    std::string why;
    if (env.type != kMsgLookupResult || env.requestId != 1 ||
        ParseLookupResult(msg.data() + kEnvelopeSize, env.payloadLen, &lr, &why) != kOk)
      die("bad LOOKUP_RESULT: %s", why.c_str());
    // One line a query, so a gate can count the answers against the
    // questions, then the matches of each (3.4: the lists come back in the
    // order the queries were asked).
    size_t cTotal = 0;
    for (const auto &q : lr.queries) cTotal += q.size();
    printf("LOOKUP queries=%zu n=%zu truncated=%d sources=%zu\n", lr.queries.size(), cTotal,
           lr.flags & 1, lr.sources.size());
    for (size_t iq = 0; iq < lr.queries.size(); iq++) {
      printf("QUERY %zu \"%s\" n=%zu\n", iq,
             iq < l.queries.size() ? l.queries[iq].c_str() : "", lr.queries[iq].size());
      for (const Match &m : lr.queries[iq])
        printf("MATCH quality=%u kind=%u %s canonical=\"%s\" designation=\"%s\"\n", m.quality,
               m.obj.kind, labelOf(m.obj).c_str(), m.canonicalName.c_str(),
               m.designation.c_str());
    }
    closeConn(fd);
    return 0;
  }

  if (count == 0) die("bad --count");

  FILE *out = outFile ? fopen(outFile, "w") : nullptr;
  if (outFile && !out) die("cannot write %s", outFile);
  FILE *lat = latencyFile ? fopen(latencyFile, "a") : nullptr;
  if (latencyFile && !lat) die("cannot write %s", latencyFile);

  const size_t nObj = req.objs.size();
  std::vector<double> cols;
  std::vector<Meta> meta;
  uint32_t nCols = 6;
  int exitCode = 0;
  uint32_t nSend = burst ? burst : 1;

  for (uint32_t rep = 0; rep < repeat && exitCode == 0; rep++) {
    struct Pending {
      uint32_t id = 0, got = 0, nextChunk = 0;
      std::vector<uint8_t> seen;
      bool done = false, refused = false, metaSet = false, cancelled = false;
    };
    std::vector<Pending> pend(nSend);
    std::string strOrder;   // the ids answered in full, in the order they were
    auto tSent = std::chrono::steady_clock::now();
    for (uint32_t k = 0; k < nSend; k++) {
      Request r2 = req;
      if (r2.timeMode == kTimeGrid) r2.start.jd1 = req.start.jd1 + (double)k * burstStep;
      if (szPriority) {
        std::vector<std::string> pr = split(szPriority, ',');
        r2.priority = (uint8_t)atoi(pr[k % pr.size()].c_str());
      }
      std::vector<uint8_t> pay;
      EncodeRequest(&pay, r2);
      pend[k].id = rep * nSend + k + 1;
      pend[k].seen.assign(count, 0);
      sendMessage(fd, kMsgRequest, pend[k].id, pay);
    }
    if (sleepMs) usleep((useconds_t)sleepMs * 1000);
    if (cancelAfterMs) {
      usleep((useconds_t)cancelAfterMs * 1000);
      sendMessage(fd, kMsgCancel, pend[0].id, std::vector<uint8_t>());
    }
    cols.assign(nObj * count * 8, 0.0);
    meta.assign(nObj, Meta());

    uint32_t nOpen = nSend;
    bool fPingSent = false;
    while (nOpen > 0 && exitCode == 0) {
      std::vector<uint8_t> msg;
      Envelope env;
      if (!readMessage(fd, &msg, &env)) {
        fprintf(stderr, "wsclient: connection closed mid-request\n");
        exitCode = 2;
        break;
      }
      Pending *pp = nullptr;
      for (Pending &p2 : pend)
        if (p2.id == env.requestId) pp = &p2;
      const uint8_t *pl = msg.data() + kEnvelopeSize;
      if (env.type == kMsgPong && fPingSent) {
        nOpen--;   // the cancelled request's answer is over, and nothing followed
        continue;
      }
      if (env.type == kMsgError) {
        Error e;
        std::string why;
        if (env.version >= kProtoMin && ParseError(pl, env.payloadLen, &e, &why) == kOk && pp) {
          if (burst && e.code == kErrBusy && !pp->done && !pp->refused) {
            pp->refused = true;
            nOpen--;
            continue;
          }
          if (cancelAfterMs && e.code == kErrCancelled && !pp->done && !pp->cancelled) {
            pp->cancelled = true;
            printf("cancel: %u of %u rows before ERROR 10\n", pp->got, count);
            // A PING: its PONG comes after anything the server still had
            // queued for the cancelled request, which must be nothing.
            sendMessage(fd, kMsgPing, 0, std::vector<uint8_t>());
            fPingSent = true;
            continue;
          }
        }
        printError(msg, env, "request");
        exitCode = 2;
        break;
      }
      if (env.type != kMsgData) continue;
      if (pp == nullptr || pp->done || pp->refused || pp->cancelled) {
        fprintf(stderr, "wsclient: DATA for request %u, which is not awaiting rows%s\n", env.requestId,
                pp && pp->cancelled ? " (it was cancelled)" : "");
        exitCode = 2;
        break;
      }
      DataChunk d;
      std::string why;
      if (ParseData(pl, env.payloadLen, &d, &why) != kOk) {
        fprintf(stderr, "wsclient: malformed DATA: %s\n", why.c_str());
        exitCode = 2;
        break;
      }
      if (d.nObj != nObj || d.totalRows != count || d.precision != req.precision ||
          d.chunkIndex != pp->nextChunk || (d.chunkIndex == 0) != ((d.flags & kChunkMeta) != 0) ||
          ((d.flags & kChunkLast) != 0) != (d.iTime + d.nRows == count)) {
        fprintf(stderr, "wsclient: DATA chunk %u does not fit request %u (nObj %u, rows %u+%u of %u, "
                "precision %u, flags 0x%x)\n", d.chunkIndex, pp->id, d.nObj, d.iTime, d.nRows, d.totalRows,
                d.precision, d.flags);
        exitCode = 2;
        break;
      }
      pp->nextChunk++;
      if (d.flags & kChunkMeta) {
        meta = d.meta;
        pp->metaSet = true;
      }
      nCols = (uint32_t)d.Cols();
      for (uint32_t rr = 0; rr < d.nRows; rr++) {
        if (pp->seen[d.iTime + rr]) {
          fprintf(stderr, "wsclient: row %u of request %u arrived twice\n", d.iTime + rr, pp->id);
          exitCode = 2;
          break;
        }
        pp->seen[d.iTime + rr] = 1;
      }
      if (exitCode) break;
      for (size_t o = 0; o < nObj; o++)
        for (uint32_t rr = 0; rr < d.nRows; rr++)
          for (uint32_t c = 0; c < nCols && c < 8; c++)
            cols[(o * count + d.iTime + rr) * 8 + c] = d.values[(o * d.nRows + rr) * nCols + c];
      pp->got += d.nRows;
      if (pp->got == count) {
        pp->done = true;
        nOpen--;
        if (!strOrder.empty()) strOrder += ",";
        strOrder += std::to_string(pp->id);
      }
    }
    uint32_t rowsGot = pend[nSend - 1].got;
    if (lat && exitCode == 0 && !burst && rowsGot == count) {
      double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - tSent).count();
      fprintf(lat, "%.0f\n", us);
    }
    if (burst && exitCode == 0) {
      uint32_t nDone = 0, nRefused = 0;
      for (const Pending &p2 : pend) { nDone += p2.done; nRefused += p2.refused; }
      printf("burst: %u answered in full, %u refused ERROR 9, order %s\n", nDone, nRefused,
             strOrder.c_str());
    }
    if (cancelAfterMs && exitCode == 0) {
      if (!pend[0].cancelled) {
        fprintf(stderr, "wsclient: the request was answered in full before the cancel (%u rows)\n",
                pend[0].got);
        exitCode = 4;
      }
      continue;
    }
    if (exitCode == 0 && !burst && rowsGot != count) {
      fprintf(stderr, "wsclient: got %u of %u rows\n", rowsGot, count);
      exitCode = 2;
    }
    if (exitCode == 0 && expectRows && rowsGot != expectRows) {
      fprintf(stderr, "wsclient: --expect-rows %u but got %u\n", expectRows, rowsGot);
      exitCode = 2;
    }
    if (exitCode == 0 && !burst && fMeta)
      for (size_t o = 0; o < nObj; o++)
        printf("META %zu %s err=%u rowsOk=%d flags=0x%x naif=%d first=%u src=%u name=\"%s\" text=\"%s\"\n", o,
               labelOf(req.objs[o]).c_str(), meta[o].errCode, meta[o].rowsOk, meta[o].flags,
               meta[o].resolvedNaif, meta[o].firstFailedRow, (unsigned)meta[o].sourceIdx, meta[o].name.c_str(),
               meta[o].errText.c_str());
    if (exitCode == 0 && out && !burst) {
      for (size_t o = 0; o < nObj; o++) {
        std::string label = labelOf(req.objs[o]);
        for (uint32_t r2 = 0; r2 < count; r2++) {
          const double *dv = cols.data() + (o * count + r2) * 8;
          fprintf(out, "%zu %s %u %d %u", o, label.c_str(), meta[o].errCode, meta[o].rowsOk, meta[o].flags);
          for (uint32_t c = 0; c < nCols && c < 8; c++) fprintf(out, " %a", dv[c]);
          fprintf(out, "\n");
        }
      }
      fflush(out);
    }
  }

  if (out) fclose(out);
  if (lat) fclose(lat);
  closeConn(fd);
  return exitCode;
}
