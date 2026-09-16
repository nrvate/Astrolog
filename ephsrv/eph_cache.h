// The ephemeris server's per-loop result cache (EPHEMERIS_SERVER_PLAN.md
// Part I section 5, "Result cache"). One instance per event loop, owned by
// that loop's thread and touched by nothing else, so it has no lock.
//
// What is cached is the COMPUTED result -- the f64 columns and the
// per-object metadata records ExecuteRequest() produces -- under a key that
// is the canonical form of the REQUEST: everything the answer depends on
// and nothing it does not. Two fields of the wire REQUEST are delivery
// parameters, not computation parameters, and are left out of the key on
// purpose: precision (f32 is converted at send, from the same f64 columns)
// and chunkRows (a framing hint). A client that asks for the same window
// first at f64 for a chart and then at f32 for animation gets one
// computation, not two. The configuration triplets are keyed only when the
// iflag bit that makes SWE read them is set, since the server applies them
// under the same condition; and SEFLG_SWIEPH|SEFLG_SPEED are folded into the
// keyed iflag because the server forces both.
//
// Entries are held by shared_ptr: a stream in flight keeps its entry alive
// after eviction, so a cache full of big windows can turn over freely while
// slow clients drain. Memory is accounted at the columns plus the metadata;
// the map and list overhead is not counted, which errs by well under one
// percent at the sizes that matter (a 30-body 1000-row window is 1.4 MB).
// An entry larger than the whole cap is computed and streamed but not
// stored, rather than evicting everything to make room for one tenant.
//
// No invalidation: requests are pure functions of static files (plan 4.7),
// and the server is stateless across restarts, which is the documented way
// to pick up a changed ephemeris tree.

#ifndef EPH_CACHE_H
#define EPH_CACHE_H

#include "ephproto.h"

#include <cstdint>
#include <cstring>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// The Swiss flags the server forces on every request (eph_srv.cpp
// ExecuteRequest): folded into the key so "iflag" and "iflag with them
// already set" are one entry. Values from swephexp.h, restated here so this
// header does not need the Swiss headers to compile in a test.
#ifndef SEFLG_SWIEPH
#define EPH_CACHE_SEFLG_SWIEPH   2L
#define EPH_CACHE_SEFLG_SPEED    256L
#define EPH_CACHE_SEFLG_JPLEPH   1L
#define EPH_CACHE_SEFLG_TOPOCTR  (32 * 1024L)
#define EPH_CACHE_SEFLG_SIDEREAL (64 * 1024L)
#else
#define EPH_CACHE_SEFLG_SWIEPH   SEFLG_SWIEPH
#define EPH_CACHE_SEFLG_SPEED    SEFLG_SPEED
#define EPH_CACHE_SEFLG_JPLEPH   SEFLG_JPLEPH
#define EPH_CACHE_SEFLG_TOPOCTR  SEFLG_TOPOCTR
#define EPH_CACHE_SEFLG_SIDEREAL SEFLG_SIDEREAL
#endif

namespace eph {

// One computed result, exactly what ExecuteRequest() fills and
// FlushStreams() reads. Immutable once inserted.
struct CacheEntry {
  uint32_t nObj = 0, nTimeRows = 0;
  std::vector<double> cols;     // object-major nObj*nTimeRows*6 f64
  std::vector<uint8_t> meta;    // nObj * kDataMetaSize
  size_t bytes() const { return cols.size() * sizeof(double) + meta.size(); }
};

// The canonical key: a byte string built from the decoded Request, so that
// it is independent of how the client happened to encode the payload and
// of the two delivery-only fields. Object records are keyed in the order
// given -- the columns come back in that order, so a permutation IS a
// different answer.
inline std::string cacheKeyOf(const Request &req) {
  std::string k;
  k.reserve(64 + req.objs.size() * 8);
  auto put = [&k](const void *p, size_t n) { k.append((const char *)p, n); };
  uint32_t nObj = (uint32_t)req.objs.size();
  put(&nObj, 4);
  for (const ObjSpec &o : req.objs) {
    put(&o.kind, 1);
    if (o.kind == kObjBody) put(&o.id, 4);
    else if (o.kind == kObjNodAps) { put(&o.id, 4); put(&o.point, 1); put(&o.method, 1); }
    else put(o.name, strlen(o.name) + 1);
  }
  put(&req.center, 4);
  // The protocol's own bits (kIflagTimeTT, kIflagCenter) stay in the key:
  // a TT instant and a UT instant are different questions.
  uint64_t iflag = req.iflag | (uint64_t)EPH_CACHE_SEFLG_SWIEPH |
                   (uint64_t)EPH_CACHE_SEFLG_SPEED;
  put(&iflag, 8);
  if (iflag & (uint64_t)EPH_CACHE_SEFLG_SIDEREAL) {
    put(&req.sidMode, 4);
    put(&req.sidT0, 8);
    put(&req.sidAyanOff, 8);
  }
  if (iflag & (uint64_t)EPH_CACHE_SEFLG_TOPOCTR) {
    put(&req.topoLon, 8);
    put(&req.topoLat, 8);
    put(&req.topoElv, 8);
  }
  if (iflag & (uint64_t)EPH_CACHE_SEFLG_JPLEPH)
    put(req.jplFile, strnlen(req.jplFile, kJplFileMax) + 1);
  put(&req.jdStart, 8);
  put(&req.stepSeconds, 4);
  put(&req.nTime, 4);
  return k;
}

// FNV-1a over the canonical key: the plan names the function, and it is
// the map's hash. Equality is still on the full key bytes, so a collision
// costs a compare, never a wrong answer.
struct CacheKeyHash {
  size_t operator()(const std::string &k) const {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : k) {
      h ^= c;
      h *= 1099511628211ULL;
    }
    return (size_t)h;
  }
};

class ResultCache {
 public:
  explicit ResultCache(size_t capBytes = 0) : cap_(capBytes) {}

  size_t capBytes() const { return cap_; }
  size_t usedBytes() const { return used_; }
  size_t entries() const { return order_.size(); }
  uint64_t hits() const { return hits_; }
  uint64_t misses() const { return misses_; }
  uint64_t evictions() const { return evictions_; }

  // A hit moves the entry to the front (most recently used). Null on a
  // miss. A cache with no capacity never hits and never counts.
  std::shared_ptr<const CacheEntry> get(const std::string &key) {
    if (cap_ == 0) return nullptr;
    auto it = map_.find(key);
    if (it == map_.end()) {
      misses_++;
      return nullptr;
    }
    hits_++;
    order_.splice(order_.begin(), order_, it->second);
    return it->second->entry;
  }

  // Insert, evicting least-recently-used entries until it fits. An entry
  // that cannot fit even in an empty cache is not stored, and the caller
  // still owns its shared_ptr for streaming. A key already present is
  // replaced (it cannot happen through the server, which looks up first,
  // but the container should not hold two of one key).
  void put(const std::string &key, std::shared_ptr<const CacheEntry> entry) {
    if (cap_ == 0 || !entry) return;
    size_t need = entry->bytes();
    if (need > cap_) return;
    auto it = map_.find(key);
    if (it != map_.end()) {
      used_ -= it->second->entry->bytes();
      order_.erase(it->second);
      map_.erase(it);
    }
    while (used_ + need > cap_ && !order_.empty()) {
      Node &victim = order_.back();
      used_ -= victim.entry->bytes();
      map_.erase(victim.key);
      order_.pop_back();
      evictions_++;
    }
    order_.push_front(Node{key, std::move(entry)});
    map_[order_.front().key] = order_.begin();
    used_ += need;
  }

 private:
  struct Node {
    std::string key;
    std::shared_ptr<const CacheEntry> entry;
  };
  size_t cap_ = 0, used_ = 0;
  uint64_t hits_ = 0, misses_ = 0, evictions_ = 0;
  std::list<Node> order_;   // front = most recently used
  std::unordered_map<std::string, std::list<Node>::iterator, CacheKeyHash> map_;
};

}  // namespace eph

#endif  // EPH_CACHE_H
