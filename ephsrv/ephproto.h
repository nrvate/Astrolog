// Ephemeris Server protocol v1, shared by the server (eph_srv.cpp) and the
// client. This file is the byte-level authority: EPHEMERIS_SERVER_PLAN.md
// Part I section 4 is the design authority and every layout here is copied
// from it exactly. All integers little-endian, all structs packed, no
// padding, no pointers. Compiled into both ends, so everything is inline
// and allocation-free except the convenience builders, which return
// std::vector buffers the caller owns.
//
// Wire discipline, stated once: the packed structs below exist to pin the
// layout and to static_assert the sizes. They are never cast onto wire
// bytes directly; every field moves through the explicit little-endian
// helpers (put*/get*, the Reader and the Writer), which are correct on any
// host byte order. Fixed float arrays are stored as exact IEEE-754 bits,
// so f64 payloads are bit-identical to the numbers swe_calc_ut_r returned.

#ifndef EPHPROTO_H
#define EPHPROTO_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// offsetof is <cstddef>; pull it in unqualified for the static_asserts below.
using std::size_t;

namespace eph {

// ---- 4.1 Envelope constants --------------------------------------------

inline constexpr uint16_t kMagic        = 0x1EF0;
inline constexpr uint8_t  kProtoVersion = 1;
inline constexpr uint16_t kDefaultPort  = 47190;

// Message types 1-7 are the ephemeris service messages and are never
// reused. Types 8+ are reserved for future non-ephemeris services.
enum MsgType : uint16_t {
  kMsgHello   = 1,  // C->S  first message after connect
  kMsgWelcome = 2,  // S->C  server identity and limits
  kMsgRequest = 3,  // C->S  ephemeris batch request
  kMsgData    = 4,  // S->C  one chunk of results
  kMsgError   = 5,  // S->C  whole-request error
  kMsgPing    = 6,  // both  heartbeat
  kMsgPong    = 7,  // both  heartbeat reply
};

// Envelope flags byte: bit0 zstd (reserved, deferred), bit1 float32.
inline constexpr uint8_t kEnvFlagZstd    = 0x01;
inline constexpr uint8_t kEnvFlagFloat32 = 0x02;
inline constexpr uint8_t kEnvFlagMask    = 0x03;

// HELLO/WELCOME caps bits (same meanings both directions).
inline constexpr uint32_t kCapFloat32 = 1u << 0;
inline constexpr uint32_t kCapZstd    = 1u << 1;

// 4.6 ERROR codes.
enum ErrCode : int32_t {
  kErrBad        = 1,  // bad request / parse
  kErrLimits     = 2,  // exceeds WELCOME limits
  kErrUnknown    = 3,  // unknown type
  kErrInternal   = 4,  // internal
  kErrEphemeris  = 5,  // ephemeris data (carries SWE serr text)
};

// REQUEST precision byte.
enum Precision : uint8_t { kPrecF64 = 0, kPrecF32 = 1 };

// REQUEST object record kind byte.
enum ObjKind : uint8_t { kObjBody = 0, kObjStar = 1 };

// WELCOME limits (server clamps/returns kErrLimits per these).
inline constexpr uint32_t kMaxObjs       = 64;
inline constexpr uint32_t kMaxRows       = 20000;
inline constexpr uint32_t kMaxChunkRows  = 500;
inline constexpr uint32_t kMaxPayload    = 4u * 1024u * 1024u;

// Field caps the layouts fix.
inline constexpr size_t kSerrMax     = 64;   // DATA metadata serr text
inline constexpr size_t kMetaNameMax = 56;   // DATA metadata body name
inline constexpr size_t kJplFileMax  = 64;   // REQUEST jplFile field
inline constexpr size_t kObjNameMax  = 96;   // host-side cap on a star name
inline constexpr size_t kColsPerObj  = 6;    // xx[0..5]

// ---- Little-endian primitives -------------------------------------------

inline void putU16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
inline void putU32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}
inline void putU64(uint8_t *p, uint64_t v) {
  for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}
inline uint16_t getU16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t getU32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline uint64_t getU64(const uint8_t *p) {
  uint64_t u = 0;
  for (int i = 7; i >= 0; i--) u = (u << 8) | p[i];
  return u;
}
inline void putI32(uint8_t *p, int32_t v) { putU32(p, (uint32_t)v); }
inline int32_t getI32(const uint8_t *p) { return (int32_t)getU32(p); }
// Floats travel as exact IEEE-754 bits, little-endian.
inline void putF64(uint8_t *p, double d) { uint64_t u; memcpy(&u, &d, 8); putU64(p, u); }
inline double getF64(const uint8_t *p) { uint64_t u = getU64(p); double d; memcpy(&d, &u, 8); return d; }
inline void putF32(uint8_t *p, float f) { uint32_t u; memcpy(&u, &f, 4); putU32(p, u); }
inline float getF32(const uint8_t *p) { uint32_t u = getU32(p); float f; memcpy(&f, &u, 4); return f; }

// ---- Bounds-checked Writer / Reader --------------------------------------
//
// The Writer never writes past its buffer: an overrun latches a fail bit
// and everything after is discarded, so a size computed with one call of
// each put* can never corrupt memory on a mismatch. The Reader behaves the
// same way on truncation. Both are cheap enough for the hot path; the
// per-request and per-chunk code below uses them only on headers and puts
// bulk data with raw().

class Writer {
public:
  Writer(uint8_t *buf, size_t cap) : p_(buf), cap_(cap) {}
  bool ok() const { return !over_; }
  size_t size() const { return pos_; }
  size_t capacity() const { return cap_; }

  void u8(uint8_t v)  { if (pos_ + 1 <= cap_) p_[pos_] = v; else over_ = true; pos_ += 1; }
  void u16(uint16_t v){ if (pos_ + 2 <= cap_) putU16(p_ + pos_, v); else over_ = true; pos_ += 2; }
  void u32(uint32_t v){ if (pos_ + 4 <= cap_) putU32(p_ + pos_, v); else over_ = true; pos_ += 4; }
  void u64(uint64_t v){ if (pos_ + 8 <= cap_) putU64(p_ + pos_, v); else over_ = true; pos_ += 8; }
  void i32(int32_t v) { u32((uint32_t)v); }
  void f64(double v)  { uint64_t u; memcpy(&u, &v, 8); u64(u); }
  void f32(float v)   { uint32_t u; memcpy(&u, &v, 4); u32(u); }

  void raw(const void *src, size_t n) {
    if (pos_ + n <= cap_) { memcpy(p_ + pos_, src, n); }
    else over_ = true;
    pos_ += n;
  }
  // NUL-terminated string including the terminator, truncated to maxLen
  // bytes (always NUL-terminated when maxLen > 0).
  void strZ(const char *s, size_t maxLen) {
    size_t n = s ? strlen(s) : 0;
    if (n >= maxLen) n = maxLen - 1;
    if (pos_ + n + 1 <= cap_) {
      memcpy(p_ + pos_, s ? s : "", n);
      p_[pos_ + n] = 0;
    } else over_ = true;
    pos_ += n + 1;
  }

private:
  uint8_t *p_;
  size_t cap_;
  size_t pos_ = 0;
  bool over_ = false;
};

class Reader {
public:
  Reader(const uint8_t *buf, size_t len) : p_(buf), len_(len) {}
  bool ok() const { return !bad_; }
  size_t left() const { return len_ - pos_; }

  uint8_t u8()   { if (pos_ + 1 <= len_) return p_[pos_++]; bad_ = true; return 0; }
  uint16_t u16() { if (pos_ + 2 <= len_) { uint16_t v = getU16(p_ + pos_); pos_ += 2; return v; } bad_ = true; return 0; }
  uint32_t u32() { if (pos_ + 4 <= len_) { uint32_t v = getU32(p_ + pos_); pos_ += 4; return v; } bad_ = true; return 0; }
  uint64_t u64() { if (pos_ + 8 <= len_) { uint64_t v = getU64(p_ + pos_); pos_ += 8; return v; } bad_ = true; return 0; }
  int32_t i32()  { return (int32_t)u32(); }
  double f64()   { uint64_t u = u64(); double d; memcpy(&d, &u, 8); return d; }
  float f32()    { uint32_t u = u32(); float f; memcpy(&f, &u, 4); return f; }

  void raw(void *dst, size_t n) {
    if (pos_ + n <= len_) { memcpy(dst, p_ + pos_, n); pos_ += n; }
    else { bad_ = true; pos_ = len_; }
  }
  // Reads to the next NUL within the buffer. Returns a pointer into the
  // input or NULL when the string is missing its terminator.
  const char *strZ(size_t *outLen = nullptr) {
    const void *nul = memchr(p_ + pos_, 0, len_ - pos_);
    if (!nul) { bad_ = true; return nullptr; }
    size_t n = (size_t)((const uint8_t *)nul - (p_ + pos_));
    const char *s = (const char *)(p_ + pos_);
    pos_ += n + 1;
    if (outLen) *outLen = n;
    return s;
  }

private:
  const uint8_t *p_;
  size_t len_;
  size_t pos_ = 0;
  bool bad_ = false;
};

// ---- 4.1 Envelope (16 bytes) --------------------------------------------

#pragma pack(push, 1)
struct EnvelopeWire {
  uint16_t magic;        // kMagic
  uint8_t  protoVersion; // kProtoVersion
  uint8_t  flags;        // bit0 zstd, bit1 float32, bits2-7 reserved 0
  uint16_t type;         // MsgType
  uint16_t reserved;     // 0
  uint32_t requestId;
  uint32_t payloadLen;   // bytes
};
#pragma pack(pop)
static_assert(sizeof(EnvelopeWire) == 16, "envelope must be exactly 16 bytes");

inline constexpr size_t kEnvelopeSize = sizeof(EnvelopeWire);

struct Envelope {
  uint16_t type;
  uint8_t flags;
  uint32_t requestId;
  uint32_t payloadLen;
};

inline void writeEnvelope(uint8_t *dst, uint16_t type, uint32_t requestId,
                          uint8_t flags, uint32_t payloadLen) {
  putU16(dst + 0, kMagic);
  dst[2] = kProtoVersion;
  dst[3] = (uint8_t)(flags & kEnvFlagMask);
  putU16(dst + 4, type);
  putU16(dst + 6, 0);
  putU32(dst + 8, requestId);
  putU32(dst + 12, payloadLen);
}

// Parses and validates magic + protocol version. Returns false on either
// mismatch (caller answers kErrBad).
inline bool parseEnvelope(const uint8_t *src, Envelope *out) {
  if (getU16(src) != kMagic) return false;
  if (src[2] != kProtoVersion) return false;
  out->type = getU16(src + 4);
  out->flags = src[3];
  out->requestId = getU32(src + 8);
  out->payloadLen = getU32(src + 12);
  return true;
}

// ---- 4.2/4.3 HELLO and WELCOME -------------------------------------------

#pragma pack(push, 1)
struct HelloWire {
  uint32_t protoVersion;
  uint32_t caps;
  uint32_t build;
  // sz version string (NUL-terminated)
};
struct WelcomeWire {
  uint32_t protoVersion;
  uint32_t caps;
  uint32_t swissephVersion; // packed major*10000 + minor*100 + patch
  uint32_t maxObjs;
  uint32_t maxRows;
  uint32_t maxChunkRows;
  uint32_t maxPayload;
  // sz serverVersion string
};
#pragma pack(pop)
static_assert(sizeof(HelloWire) == 12, "HELLO fixed part must be 12 bytes");
static_assert(sizeof(WelcomeWire) == 28, "WELCOME fixed part must be 28 bytes");

inline void buildHello(uint8_t *dst, uint32_t caps, uint32_t build,
                       const char *version, uint32_t *payloadLen) {
  Writer w(dst, sizeof(HelloWire) + 256);
  w.u32(kProtoVersion);
  w.u32(caps);
  w.u32(build);
  w.strZ(version ? version : "", 256);
  *payloadLen = (uint32_t)w.size();
}

struct Hello {
  uint32_t protoVersion;
  uint32_t caps;
  uint32_t build;
  std::string version;
};

inline bool parseHello(const uint8_t *p, size_t len, Hello *out) {
  if (len < sizeof(HelloWire)) return false;
  Reader r(p, len);
  out->protoVersion = r.u32();
  out->caps = r.u32();
  out->build = r.u32();
  size_t n;
  const char *s = r.strZ(&n);
  if (!r.ok() || !s) return false;
  out->version.assign(s, n);
  return true;
}

inline void buildWelcome(uint8_t *dst, uint32_t caps, uint32_t swissephVersion,
                         const char *serverVersion, uint32_t *payloadLen) {
  Writer w(dst, sizeof(WelcomeWire) + 256);
  w.u32(kProtoVersion);
  w.u32(caps);
  w.u32(swissephVersion);
  w.u32(kMaxObjs);
  w.u32(kMaxRows);
  w.u32(kMaxChunkRows);
  w.u32(kMaxPayload);
  w.strZ(serverVersion ? serverVersion : "", 256);
  *payloadLen = (uint32_t)w.size();
}

struct Welcome {
  uint32_t protoVersion;
  uint32_t caps;
  uint32_t swissephVersion;
  uint32_t maxObjs;
  uint32_t maxRows;
  uint32_t maxChunkRows;
  uint32_t maxPayload;
  std::string serverVersion;
};

inline bool parseWelcome(const uint8_t *p, size_t len, Welcome *out) {
  if (len < sizeof(WelcomeWire)) return false;
  Reader r(p, len);
  out->protoVersion = r.u32();
  out->caps = r.u32();
  out->swissephVersion = r.u32();
  out->maxObjs = r.u32();
  out->maxRows = r.u32();
  out->maxChunkRows = r.u32();
  out->maxPayload = r.u32();
  size_t n;
  const char *s = r.strZ(&n);
  if (!r.ok() || !s) return false;
  out->serverVersion.assign(s, n);
  return true;
}

// ---- 4.4 REQUEST ----------------------------------------------------------
//
//   u32 nObj
//   nObj object records:
//     u8 kind (0 body by id, 1 fixed star by name)
//     kind 0: u32 id
//     kind 1: sz name
//   then RequestFixed (141 bytes), fields exactly as the plan lists them.

#pragma pack(push, 1)
struct RequestFixed {
  int32_t center;      // 0 = use iflag center bits; else swe_calc_pctr body id
  uint64_t iflag;      // full SWE bitmask; server ORs in SEFLG_SWIEPH
  int32_t sidMode;     // swe_set_sid_mode_r triple, only with SEFLG_SIDEREAL
  double sidT0;
  double sidAyanOff;
  double topoLon;      // east-positive degrees; only with SEFLG_TOPOCTR
  double topoLat;
  double topoElv;      // km
  char jplFile[64];    // swe_set_jpl_file_r; only with SEFLG_JPLEPH
  double jdStart;      // UT
  uint32_t stepSeconds;
  uint32_t nTime;      // rows
  uint8_t precision;   // 0 = f64, 1 = f32
  uint32_t chunkRows;  // client hint; server clamps to maxChunkRows
};
#pragma pack(pop)
static_assert(sizeof(RequestFixed) == 141, "REQUEST fixed part must be 141 bytes");

// One object record on the wire:
//   kind 0: 1 byte kind + 4 bytes id           = 5 bytes
//   kind 1: 1 byte kind + strlen + 1 byte NUL  = 2 + strlen bytes
inline constexpr size_t kObjRecordBodySize = 5;

struct ObjSpec {
  uint8_t kind = kObjBody;
  uint32_t id = 0;              // kObjBody: raw SWE id
  char name[kObjNameMax] = {0}; // kObjStar: NUL-terminated star name
};

// Host-side, fully decoded REQUEST. Owned by the caller (the vector is the
// one allocation the caller owns; decode fills it).
struct Request {
  std::vector<ObjSpec> objs;
  int32_t center = 0;
  uint64_t iflag = 0;
  int32_t sidMode = 0;
  double sidT0 = 0.0, sidAyanOff = 0.0;
  double topoLon = 0.0, topoLat = 0.0, topoElv = 0.0;
  char jplFile[kJplFileMax] = {0};
  double jdStart = 0.0;
  uint32_t stepSeconds = 0, nTime = 0;
  uint8_t precision = kPrecF64;
  uint32_t chunkRows = 0;
};

enum ParseResult { kParseOk = 0, kParseBad = 1, kParseLimits = 2 };

// Decodes a REQUEST payload. Well-formedness failures return kParseBad
// (ERROR code 1); values beyond the WELCOME limits return kParseLimits
// (ERROR code 2). chunkRows above kMaxChunkRows is NOT a limit failure --
// the plan defines it as a hint the server clamps -- so the caller clamps.
inline ParseResult parseRequest(const uint8_t *p, size_t len, Request *out) {
  if (len < 4) return kParseBad;
  Reader r(p, len);
  uint32_t nObj = r.u32();
  if (!r.ok()) return kParseBad;
  if (nObj == 0) return kParseBad;
  if (nObj > kMaxObjs) return kParseLimits;

  out->objs.resize(nObj);
  for (uint32_t i = 0; i < nObj; i++) {
    ObjSpec &o = out->objs[i];
    uint8_t kind = r.u8();
    if (kind == kObjBody) {
      o.kind = kObjBody;
      o.id = r.u32();
    } else if (kind == kObjStar) {
      size_t n;
      const char *s = r.strZ(&n);
      if (!r.ok() || !s || n + 1 > kObjNameMax) return kParseBad;
      o.kind = kObjStar;
      memcpy(o.name, s, n + 1);
    } else {
      return kParseBad;
    }
  }

  out->center = r.i32();
  out->iflag = r.u64();
  out->sidMode = r.i32();
  out->sidT0 = r.f64();
  out->sidAyanOff = r.f64();
  out->topoLon = r.f64();
  out->topoLat = r.f64();
  out->topoElv = r.f64();
  r.raw(out->jplFile, kJplFileMax);
  out->jplFile[kJplFileMax - 1] = 0;  // defensive; encoder NUL-pads anyway
  out->jdStart = r.f64();
  out->stepSeconds = r.u32();
  out->nTime = r.u32();
  out->precision = r.u8();
  out->chunkRows = r.u32();
  if (!r.ok()) return kParseBad;

  if (out->nTime == 0) return kParseBad;
  if (out->nTime > kMaxRows) return kParseLimits;
  if (out->precision > kPrecF32) return kParseBad;
  if (!r.ok() || r.left() != 0) return kParseBad;  // trailing bytes
  return kParseOk;
}

// Encodes a REQUEST (client side / tests). Exact inverse of parseRequest.
inline void buildRequest(std::vector<uint8_t> *out, const Request &req) {
  size_t sz = 4;
  for (const ObjSpec &o : req.objs)
    sz += (o.kind == kObjBody) ? kObjRecordBodySize : (1 + strlen(o.name) + 1);
  sz += sizeof(RequestFixed);
  out->resize(sz);
  Writer w(out->data(), sz);
  w.u32((uint32_t)req.objs.size());
  for (const ObjSpec &o : req.objs) {
    w.u8(o.kind);
    if (o.kind == kObjBody) w.u32(o.id);
    else w.strZ(o.name, kObjNameMax);
  }
  w.i32(req.center);
  w.u64(req.iflag);
  w.i32(req.sidMode);
  w.f64(req.sidT0);
  w.f64(req.sidAyanOff);
  w.f64(req.topoLon);
  w.f64(req.topoLat);
  w.f64(req.topoElv);
  w.raw(req.jplFile, kJplFileMax);
  w.f64(req.jdStart);
  w.u32(req.stepSeconds);
  w.u32(req.nTime);
  w.u8(req.precision);
  w.u32(req.chunkRows);
}

// ---- 4.5 DATA (one chunk) -------------------------------------------------
//
//   u32 chunkIndex
//   u32 iTime     (first row index in this chunk)
//   u32 nTime     (rows in this chunk)
//   u8  precision
//   u32 nObj
//   nObj DataMeta records
//   data block: object-major, nObj * nTime * 6 values (f64 or f32)

#pragma pack(push, 1)
struct DataWire {
  uint32_t chunkIndex;
  uint32_t iTime;
  uint32_t nTimeRows;
  uint8_t precision;
  uint32_t nObj;
};
struct DataMetaWire {
  int32_t retFlag;    // <0: this object failed; else flags SWE actually used
  int32_t flagsUsed;
  char serr[64];      // SWE error text when retFlag < 0, else zero-filled
  char name[56];      // body name
};
#pragma pack(pop)
static_assert(sizeof(DataWire) == 17, "DATA fixed header must be 17 bytes");
static_assert(sizeof(DataMetaWire) == 128, "per-object metadata must be 128 bytes");
static_assert(offsetof(DataMetaWire, serr) == 8, "serr field must start at byte 8");
static_assert(offsetof(DataMetaWire, name) == 72, "name field must start at byte 72");
static_assert(kSerrMax == 64 && kMetaNameMax == 56, "serr/name field caps");

inline constexpr size_t kDataHeaderSize = sizeof(DataWire);
inline constexpr size_t kDataMetaSize = sizeof(DataMetaWire);

inline size_t dataPayloadSize(size_t nObj, size_t nTimeRows, uint8_t precision) {
  size_t esz = (precision == kPrecF32) ? 4 : 8;
  return kDataHeaderSize + nObj * kDataMetaSize + nObj * nTimeRows * kColsPerObj * esz;
}

// Header + metadata + the values for one chunk, written into a
// caller-owned buffer sized by dataPayloadSize(). The values live in cols
// as f64 in the plan's object-major layout -- object o, row r, column c at
// cols[(o * totalRows + r) * 6 + c] -- and totalRows is the request's full
// row count (chunks are contiguous row ranges, but the layout is
// object-major, so the chunk's rows are gathered per object here). When
// precision is f32 the conversion happens here, at send, exactly as the
// plan says.
inline void writeDataChunk(uint8_t *dst, size_t cap,
                           uint32_t chunkIndex, uint32_t iTime,
                           uint32_t nTimeRows, uint32_t totalRows,
                           uint32_t nObj, uint8_t precision,
                           const uint8_t *meta /* nObj * 128 */,
                           const double *cols /* object-major f64 */,
                           size_t *outLen) {
  Writer w(dst, cap);
  w.u32(chunkIndex);
  w.u32(iTime);
  w.u32(nTimeRows);
  w.u8(precision);
  w.u32(nObj);
  w.raw(meta, nObj * kDataMetaSize);
  for (uint32_t o = 0; o < nObj; o++) {
    const double *src = cols + ((size_t)o * totalRows + iTime) * kColsPerObj;
    size_t n = (size_t)nTimeRows * kColsPerObj;
    if (precision == kPrecF32) {
      for (size_t i = 0; i < n; i++) w.f32((float)src[i]);
    } else {
      for (size_t i = 0; i < n; i++) w.f64(src[i]);
    }
  }
  *outLen = w.size();
}

// Just the per-object metadata record (128 bytes): retFlag, flagsUsed,
// then serr[64] and name[56], each NUL-padded and truncated to fit.
inline void writeDataMeta(uint8_t *dst, int32_t retFlag, int32_t flagsUsed,
                          const char *serr, const char *name) {
  memset(dst, 0, kDataMetaSize);
  Writer w(dst, kDataMetaSize);
  w.i32(retFlag);
  w.i32(flagsUsed);
  {
    char buf[kSerrMax] = {0};
    if (serr) {
      size_t n = strlen(serr);
      if (n >= kSerrMax) n = kSerrMax - 1;
      memcpy(buf, serr, n);
    }
    w.raw(buf, kSerrMax);
  }
  {
    char buf[kMetaNameMax] = {0};
    if (name) {
      size_t n = strlen(name);
      if (n >= kMetaNameMax) n = kMetaNameMax - 1;
      memcpy(buf, name, n);
    }
    w.raw(buf, kMetaNameMax);
  }
}

// ---- 4.6 ERROR ------------------------------------------------------------

#pragma pack(push, 1)
struct ErrorWire {
  uint32_t requestId;
  int32_t code;
  // sz text
};
#pragma pack(pop)
static_assert(sizeof(ErrorWire) == 8, "ERROR fixed part must be 8 bytes");

struct ErrorMsg {
  uint32_t requestId;
  int32_t code;
  std::string text;
};

inline bool parseError(const uint8_t *p, size_t len, ErrorMsg *out) {
  if (len < sizeof(ErrorWire)) return false;
  Reader r(p, len);
  out->requestId = r.u32();
  out->code = r.i32();
  size_t n;
  const char *s = r.strZ(&n);
  if (!r.ok() || !s) return false;
  out->text.assign(s, n);
  return true;
}

// ---- Envelope + payload builders (convenience; caller owns the vector) ----

inline std::vector<uint8_t> makeMessage(uint16_t type, uint32_t requestId,
                                        const void *payload, size_t payloadLen,
                                        uint8_t flags = 0) {
  std::vector<uint8_t> msg(kEnvelopeSize + payloadLen);
  writeEnvelope(msg.data(), type, requestId, flags, (uint32_t)payloadLen);
  if (payloadLen) memcpy(msg.data() + kEnvelopeSize, payload, payloadLen);
  return msg;
}

inline std::vector<uint8_t> buildPing(uint32_t requestId) {
  return makeMessage(kMsgPing, requestId, nullptr, 0);
}
inline std::vector<uint8_t> buildPong(uint32_t requestId) {
  return makeMessage(kMsgPong, requestId, nullptr, 0);
}

// ---- FNV-1a over canonical payload bytes (5, result cache key) ------------

inline uint64_t fnv1a64(const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  uint64_t h = 14695981039346656037ull;          // FNV offset basis
  const uint64_t prime = 1099511628211ull;       // FNV prime
  for (size_t i = 0; i < len; i++) {
    h ^= (uint64_t)p[i];
    h *= prime;
  }
  return h;
}

}  // namespace eph

#endif  // EPHPROTO_H
