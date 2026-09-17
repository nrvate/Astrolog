// eph_wsclient: standalone single-connection WebSocket client for the
// ephemeris server, used by tools/ephsrv-golden.sh and tools/ephsrv-soak.sh.
// Raw sockets only: uWS v20.80.0 has no client implementation, so framing is
// hand-rolled after ephsrv/smoketest.cpp. Protocol comes from ephsrv/
// ephproto.h (included directly; it is the byte-level authority).
//
//   eph_wsclient [--host H] [--port P] [--out FILE] [--objs id[,id...]]
//                [--stars "name[,name...]"] [--jd JD] [--step SEC] [--count N]
//                [--precision 64|32] [--center N] [--iflag HEX]
//                [--sid mode,t0,offset] [--topo lon,lat,elv] [--jplfile name]
//                [--expect-rows N] [--repeat N] [--latency FILE] [--quiet]
//                [--tt] [--nodaps id,point,method[;...]]
//                [--sleep-ms MS] [--burst N] [--cycles N] [--hello N]
//                [--tls] [--ca FILE] [--sni NAME]
//
// Sends HELLO, prints WELCOME unless --quiet, sends one REQUEST, collects
// the DATA chunks, and on --out writes one line per object per row in
// hexfloat: "<objIdx> <id-or-name> <retFlag> <lon> <lat> <dist> <slon>
// <slat> <sdist>". Exits 0 on success; on a server ERROR the text goes to
// stderr and the exit is 2. --repeat re-runs the request N times on the
// same connection (the soak gate's fd-stability barrage). --latency
// appends one line per repeat to FILE: the microseconds from the REQUEST
// send to the last DATA chunk's arrival, which is what a client sees and
// what tools/ephsrv-bench.sh aggregates. --tt marks --jd as TT rather than
// UT (kIflagTimeTT); --nodaps adds node/apsis records (kind 2: point 1-4,
// method 0 mean / 1 osculating).
//
// For tools/ephsrv-robust.sh: --sleep-ms reads nothing for MS after each
// REQUEST is sent, so the server's answer backs up behind it; --burst N
// sends N requests at once (jd advanced --burst-step days each, default 1,
// so none share a cache entry; 0 makes them one cached answer) before reading any answer, and prints how many were answered and
// how many refused with ERROR 2; --cycles N only connects, is welcomed and
// closes, N times; --hello N sends HELLO N times on one connection and
// requires a WELCOME for each. Every answer is checked row by row: a row that arrives
// twice, a row that never arrives, a chunk for another request or a chunk
// whose rows run past the window is a failure, whatever the row count.
//
// --tls speaks wss://: TLS over the same socket, verifying the server's
// certificate against --ca (a PEM file; the system store without it) and
// its name against --sni, or --host when --sni is not given. Verification
// is never switched off -- a gate that wants to see a bad certificate
// refused has to see the refusal. A failed handshake exits 3 with
// OpenSSL's reason on stderr, so a gate can tell it from a refused
// connection (2).

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

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
        putU16(frame + f, (uint16_t)n); f += 2;
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

int main(int argc, char **argv) {
  const char *host = "127.0.0.1", *outFile = nullptr, *szStars = nullptr,
             *szSid = nullptr, *szTopo = nullptr, *szJpl = nullptr,
             *latencyFile = nullptr, *szNodAps = nullptr;
  bool fTT = false, fTls = false;
  const char *szCa = nullptr, *szSni = nullptr;
  uint16_t port = kDefaultPort;
  std::vector<uint32_t> ids;
  double jd = 2451545.0;
  uint32_t step = 600, count = 5, repeat = 1, expectRows = 0;
  int precision = 0, center = 0;
  uint64_t iflag = 0;
  bool quiet = false;
  uint32_t sleepMs = 0, burst = 0, cycles = 0, nHello = 1;
  double burstStep = 1.0;
  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    auto next = [&](const char **v) { if (i + 1 < argc) { *v = argv[++i]; return true; } return false; };
    const char *v;
    if (!strcmp(a, "--host") && next(&v)) host = v;
    else if (!strcmp(a, "--port") && next(&v)) port = (uint16_t)atoi(v);
    else if (!strcmp(a, "--out") && next(&v)) outFile = v;
    else if (!strcmp(a, "--objs") && next(&v)) {
      for (char *tok = strtok((char *)v, ","); tok; tok = strtok(nullptr, ","))
        ids.push_back((uint32_t)strtoul(tok, nullptr, 10));
    }
    else if (!strcmp(a, "--stars") && next(&v)) szStars = v;
    else if (!strcmp(a, "--jd") && next(&v)) jd = atof(v);
    else if (!strcmp(a, "--step") && next(&v)) step = (uint32_t)strtoul(v, nullptr, 10);
    else if (!strcmp(a, "--count") && next(&v)) count = (uint32_t)strtoul(v, nullptr, 10);
    else if (!strcmp(a, "--precision") && next(&v)) precision = atoi(v);
    else if (!strcmp(a, "--center") && next(&v)) center = atoi(v);
    else if (!strcmp(a, "--iflag") && next(&v)) iflag = strtoull(v, nullptr, 16);
    else if (!strcmp(a, "--sid") && next(&v)) szSid = v;
    else if (!strcmp(a, "--topo") && next(&v)) szTopo = v;
    else if (!strcmp(a, "--jplfile") && next(&v)) szJpl = v;
    else if (!strcmp(a, "--expect-rows") && next(&v)) expectRows = (uint32_t)strtoul(v, nullptr, 10);
    else if (!strcmp(a, "--repeat") && next(&v)) repeat = (uint32_t)strtoul(v, nullptr, 10);
    else if (!strcmp(a, "--latency") && next(&v)) latencyFile = v;
    else if (!strcmp(a, "--tt")) fTT = true;
    else if (!strcmp(a, "--nodaps") && next(&v)) szNodAps = v;
    else if (!strcmp(a, "--quiet")) quiet = true;
    else if (!strcmp(a, "--sleep-ms") && next(&v)) sleepMs = (uint32_t)strtoul(v, nullptr, 10);
    else if (!strcmp(a, "--burst") && next(&v)) burst = (uint32_t)strtoul(v, nullptr, 10);
    else if (!strcmp(a, "--cycles") && next(&v)) cycles = (uint32_t)strtoul(v, nullptr, 10);
    else if (!strcmp(a, "--hello") && next(&v)) nHello = (uint32_t)strtoul(v, nullptr, 10);
    else if (!strcmp(a, "--burst-step") && next(&v)) burstStep = atof(v);
    else if (!strcmp(a, "--tls")) fTls = true;
    else if (!strcmp(a, "--ca") && next(&v)) szCa = v;
    else if (!strcmp(a, "--sni") && next(&v)) szSni = v;
    else { fprintf(stderr, "wsclient: unknown/incomplete option %s\n", a); return 1; }
  }

  Request req;
  for (uint32_t id : ids) {
    ObjSpec o;
    o.kind = kObjBody;
    o.id = id;
    req.objs.push_back(o);
  }
  if (szStars) {
    char *buf = strdup(szStars);
    for (char *tok = strtok(buf, ","); tok; tok = strtok(nullptr, ",")) {
      ObjSpec o;
      o.kind = kObjStar;
      snprintf(o.name, sizeof(o.name), "%s", tok);
      req.objs.push_back(o);
    }
    free(buf);
  }
  if (szNodAps) {
    char *buf = strdup(szNodAps);
    for (char *tok = strtok(buf, ";"); tok; tok = strtok(nullptr, ";")) {
      ObjSpec o;
      unsigned id = 0, point = 0, method = 0;
      if (sscanf(tok, "%u,%u,%u", &id, &point, &method) != 3) {
        fprintf(stderr, "wsclient: bad --nodaps entry %s\n", tok);
        return 1;
      }
      o.kind = kObjNodAps;
      o.id = id;
      o.point = (uint8_t)point;
      o.method = (uint8_t)method;
      req.objs.push_back(o);
    }
    free(buf);
  }
  req.center = center;
  req.iflag = iflag | (fTT ? kIflagTimeTT : 0);
  if (szSid) sscanf(szSid, "%d,%lf,%lf", &req.sidMode, &req.sidT0, &req.sidAyanOff);
  if (szTopo) sscanf(szTopo, "%lf,%lf,%lf", &req.topoLon, &req.topoLat, &req.topoElv);
  if (szJpl) snprintf(req.jplFile, sizeof(req.jplFile), "%s", szJpl);
  req.jdStart = jd;
  req.stepSeconds = step;
  req.nTime = count;
  req.precision = precision ? (uint8_t)kPrecF32 : (uint8_t)kPrecF64;
  req.chunkRows = kMaxChunkRows;
  if (req.objs.empty()) { fprintf(stderr, "wsclient: --objs or --stars required\n"); return 1; }
  if (count == 0 || count > kMaxRows) { fprintf(stderr, "wsclient: bad --count\n"); return 1; }

  if (fTls) {
    gSslCtx = SSL_CTX_new(TLS_client_method());
    SSL_CTX_set_min_proto_version(gSslCtx, TLS1_2_VERSION);
    SSL_CTX_set_verify(gSslCtx, SSL_VERIFY_PEER, nullptr);
    if (szCa ? SSL_CTX_load_verify_locations(gSslCtx, szCa, nullptr) != 1
             : SSL_CTX_set_default_verify_paths(gSslCtx) != 1)
      tlsFailed("loading the CA");
  }

  int fd;
  for (uint32_t cyc = 0; cyc <= cycles; cyc++) {
  if (!connectWs(host, port, &fd, szSni)) {
    fprintf(stderr, "wsclient: cannot connect to %s:%u\n", host, (unsigned)port);
    return 2;
  }

  // HELLO, expect WELCOME -- --hello N times on this one connection.
  for (uint32_t h = 0; h < (nHello ? nHello : 1); h++) {
    uint8_t hbuf[sizeof(HelloWire) + 256];
    uint32_t hlen = 0;
    buildHello(hbuf, 0, 0, "eph_wsclient/1.0", &hlen);
    sendWsBinary(fd, makeMessage(kMsgHello, 0, hbuf, hlen));
    std::vector<uint8_t> msg;
    if (readWsMessage(fd, &msg) != kGotMessage || msg.size() < kEnvelopeSize) {
      fprintf(stderr, "wsclient: no WELCOME envelope\n");
      return 2;
    }
    Envelope env;
    if (!parseEnvelope(msg.data(), &env)) {
      fprintf(stderr, "wsclient: bad WELCOME envelope\n");
      return 2;
    }
    if (env.type != kMsgWelcome) {
      fprintf(stderr, "wsclient: expected WELCOME, got type %u\n", env.type);
      return 2;
    }
    Welcome w;
    if (!parseWelcome(msg.data() + kEnvelopeSize, env.payloadLen, &w)) {
      fprintf(stderr, "wsclient: malformed WELCOME\n");
      return 2;
    }
    if (!quiet)
      printf("WELCOME v%u caps=0x%x swe=%u maxObjs=%u maxRows=%u maxChunk=%u "
             "maxCells=%u server=%s\n", w.protoVersion, w.caps,
             w.swissephVersion, w.maxObjs, w.maxRows, w.maxChunkRows,
             w.maxCells, w.serverVersion.c_str());
  }
  if (cyc < cycles) {
    closeConn(fd);
    gPending.clear();
    if (cyc + 1 == cycles) return 0;   // --cycles: connections only
  }
  }

  FILE *out = outFile ? fopen(outFile, "w") : nullptr;
  if (outFile && !out) {
    fprintf(stderr, "wsclient: cannot write %s\n", outFile);
    return 2;
  }
  FILE *lat = latencyFile ? fopen(latencyFile, "a") : nullptr;
  if (latencyFile && !lat) {
    fprintf(stderr, "wsclient: cannot write %s\n", latencyFile);
    return 2;
  }

  // Full column store: object-major nObj * count * 6; metadata from the
  // first chunk (identical across chunks), and which rows have landed.
  std::vector<double> cols((size_t)req.objs.size() * count * 6, 0.0);
  std::vector<uint8_t> meta((size_t)req.objs.size() * kDataMetaSize, 0);
  int exitCode = 0;
  uint32_t nSend = burst ? burst : 1;

  for (uint32_t rep = 0; rep < repeat && exitCode == 0; rep++) {
    // One or (--burst) several requests, ids rep*nSend+1.., sent before
    // any answer is read.
    struct Pending { uint32_t id; std::vector<uint8_t> seen; uint32_t got = 0;
                     bool done = false, refused = false; bool metaSet = false; };
    std::vector<Pending> pend(nSend);
    auto tSent = std::chrono::steady_clock::now();
    for (uint32_t k = 0; k < nSend; k++) {
      Request r2 = req;
      r2.jdStart = req.jdStart + (double)k * burstStep;
      std::vector<uint8_t> payload;
      buildRequest(&payload, r2);
      pend[k].id = rep * nSend + k + 1;
      pend[k].seen.assign(count, 0);
      sendWsBinary(fd, makeMessage(kMsgRequest, pend[k].id, payload.data(), payload.size()));
    }
    if (sleepMs) usleep((useconds_t)sleepMs * 1000);
    std::fill(cols.begin(), cols.end(), 0.0);
    std::fill(meta.begin(), meta.end(), 0);

    uint32_t nOpen = nSend;
    while (nOpen > 0 && exitCode == 0) {
      std::vector<uint8_t> msg;
      int st = readWsMessage(fd, &msg);
      if (st != kGotMessage || msg.size() < kEnvelopeSize) {
        fprintf(stderr, "wsclient: connection closed mid-request\n");
        exitCode = 2;
        break;
      }
      Envelope env;
      if (!parseEnvelope(msg.data(), &env) ||
          (size_t)env.payloadLen != msg.size() - kEnvelopeSize) {
        fprintf(stderr, "wsclient: bad envelope on message of %zu bytes\n",
                msg.size());
        exitCode = 2;
        break;
      }
      Pending *pp = nullptr;
      for (Pending &p2 : pend)
        if (p2.id == env.requestId && !p2.done && !p2.refused) pp = &p2;
      const uint8_t *pl = msg.data() + kEnvelopeSize;
      if (env.type == kMsgError) {
        ErrorMsg e;
        if (!parseError(pl, env.payloadLen, &e)) {
          fprintf(stderr, "wsclient: malformed ERROR\n");
          exitCode = 2;
          break;
        }
        if (burst && pp != nullptr && e.code == kErrLimits) {
          pp->refused = true;
          nOpen--;
          continue;
        }
        fprintf(stderr, "wsclient: server ERROR %d (request %u): %s\n",
                e.code, e.requestId, e.text.c_str());
        exitCode = 2;
        break;
      }
      if (env.type != kMsgData) continue;   // ignore anything else
      if (pp == nullptr) {
        fprintf(stderr, "wsclient: DATA for request %u, which is not awaiting "
                "rows\n", env.requestId);
        exitCode = 2;
        break;
      }

      if (env.payloadLen < kDataHeaderSize) { exitCode = 2; break; }
      Reader r(pl, env.payloadLen);
      uint32_t chunkIndex = r.u32(), iTime = r.u32(), nRows = r.u32();
      (void)chunkIndex;
      uint8_t prec = r.u8();
      uint32_t nObj = r.u32();
      if (!r.ok() || nObj != (uint32_t)req.objs.size() || nRows == 0 ||
          (uint64_t)iTime + nRows > count) {
        fprintf(stderr, "wsclient: bad DATA chunk header\n");
        exitCode = 2;
        break;
      }
      if (!pp->metaSet) {
        r.raw(meta.data(), nObj * kDataMetaSize);
        pp->metaSet = true;
      } else {
        std::vector<uint8_t> skip((size_t)nObj * kDataMetaSize);
        r.raw(skip.data(), skip.size());
      }
      if (!r.ok()) { fprintf(stderr, "wsclient: truncated DATA meta\n"); exitCode = 2; break; }
      size_t nVals = (size_t)nObj * nRows * kColsPerObj;
      size_t esz = (prec == kPrecF32) ? 4 : 8;
      std::vector<uint8_t> vals(nVals * esz);
      r.raw(vals.data(), vals.size());
      if (!r.ok() || r.left() != 0) { fprintf(stderr, "wsclient: DATA values not the size the header says\n"); exitCode = 2; break; }
      for (uint32_t rr = 0; rr < nRows; rr++) {
        if (pp->seen[iTime + rr]) {
          fprintf(stderr, "wsclient: row %u of request %u arrived twice\n",
                  iTime + rr, pp->id);
          exitCode = 2;
          break;
        }
        pp->seen[iTime + rr] = 1;
      }
      if (exitCode) break;
      for (uint32_t o = 0; o < nObj; o++) {
        double *dst = cols.data() + ((size_t)o * count + iTime) * kColsPerObj;
        const uint8_t *src = vals.data() + ((size_t)o * nRows) * kColsPerObj * esz;
        for (uint32_t rr = 0; rr < nRows; rr++)
          for (int c = 0; c < (int)kColsPerObj; c++) {
            dst[(size_t)rr * kColsPerObj + c] =
              (prec == kPrecF32) ? (double)getF32(src + ((size_t)rr * kColsPerObj + c) * 4)
                                 : getF64(src + ((size_t)rr * kColsPerObj + c) * 8);
          }
      }
      pp->got += nRows;
      if (pp->got == count) {
        pp->done = true;
        nOpen--;
      }
    }
    uint32_t rowsGot = pend[nSend - 1].got;
    if (lat && exitCode == 0 && !burst && rowsGot == count) {
      double us = std::chrono::duration<double, std::micro>(
          std::chrono::steady_clock::now() - tSent).count();
      fprintf(lat, "%.0f\n", us);
    }
    if (burst && exitCode == 0) {
      uint32_t nDone = 0, nRefused = 0;
      for (const Pending &p2 : pend) { nDone += p2.done; nRefused += p2.refused; }
      printf("burst: %u answered in full, %u refused ERROR 2\n", nDone, nRefused);
    }
    if (exitCode == 0 && !burst && rowsGot != count) {
      fprintf(stderr, "wsclient: got %u of %u rows\n", rowsGot, count);
      exitCode = 2;
    }
    if (exitCode == 0 && expectRows && rowsGot != expectRows) {
      fprintf(stderr, "wsclient: --expect-rows %u but got %u\n", expectRows, rowsGot);
      exitCode = 2;
    }
    if (exitCode == 0 && out && !burst) {
      for (size_t o = 0; o < req.objs.size(); o++) {
        int32_t retFlag = getI32(meta.data() + o * kDataMetaSize);
        const ObjSpec &o2 = req.objs[o];
        char szWho[128];
        if (o2.kind == kObjBody) snprintf(szWho, sizeof(szWho), "%u", o2.id);
        else if (o2.kind == kObjNodAps)
          snprintf(szWho, sizeof(szWho), "%u,%u,%u", o2.id, o2.point, o2.method);
        else snprintf(szWho, sizeof(szWho), "%s", o2.name);
        for (uint32_t r2 = 0; r2 < count; r2++) {
          const double *d = cols.data() + ((size_t)o * count + r2) * kColsPerObj;
          fprintf(out, "%zu %s %d %a %a %a %a %a %a\n", o, szWho, retFlag,
                  d[0], d[1], d[2], d[3], d[4], d[5]);
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
