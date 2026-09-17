// The ephemeris server's per-loop result cache (EPHEMERIS_PLUGINS_PLAN.md
// 3.7). One instance per event loop, owned by that loop's thread and
// touched by nothing else, so it has no lock.
//
// What is cached is the COMPUTED answer -- the f64 columns, the source
// table and the per-object metadata ExecuteRequest() produces -- under the
// key protocol version 4 defines: the server's datasetId followed by the
// REQUEST's question block, byte for byte as received. The delivery block
// (precision, priority, chunkRows) stays out of it, so a window asked at f64
// for a chart and again at f32 in small chunks for animation is one
// computation. Canonical encoding (3.1: a parser refuses every other
// spelling) is what makes equal questions equal bytes; nothing here
// normalises anything. datasetId changes whenever an answer could, so a
// key can never outlive the files it was computed from.
//
// Entries are held by shared_ptr: a stream in flight keeps its entry alive
// after eviction, so a cache full of big windows can turn over freely while
// slow clients drain. Memory is accounted at the columns plus the metadata's
// strings plus the key (held twice, in the list node and the map) and a
// fixed allowance for the nodes and allocation headers. Without the last
// two a one-object one-row entry counted 176 bytes against ~550 real, so a
// cache of chart casts ran to about three times --cache-mb. An entry larger
// than the whole cap is computed and streamed but not stored, rather than
// evicting everything to make room for one tenant.

#ifndef EPH_CACHE_H
#define EPH_CACHE_H

#include "ephproto.h"

#include <cstdint>
#include <cstring>
#include <list>
#include <random>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eph {

// One computed answer, exactly what ExecuteRequest() fills and
// FlushStreams() reads. Immutable once inserted.
struct CacheEntry {
  uint32_t nObj = 0, nTimeRows = 0;
  uint32_t columnsPresent = 0;           // DATA's columnsPresent
  uint32_t nCols = 6;                    // 6 + popcount(columnsPresent)
  std::vector<double> cols;              // object-major nObj*nTimeRows*nCols
  std::vector<std::string> sources;      // the source table
  std::vector<Meta> meta;                // nObj
  size_t bytes() const {
    size_t n = cols.size() * sizeof(double) + meta.size() * sizeof(Meta);
    for (const Meta &m : meta) n += m.name.size() + m.errText.size();
    for (const std::string &s : sources) n += sizeof(std::string) + s.size();
    return n;
  }
};

// 3.7: datasetId, then the question block exactly as received. The
// datasetId goes in with its length, so no two (dataset, question) pairs
// make the same key.
inline std::string CacheKey(const std::string &datasetId, const uint8_t *payload,
                            size_t payloadLen, size_t questionOffset) {
  std::string k;
  size_t q = questionOffset <= payloadLen ? payloadLen - questionOffset : 0;
  k.reserve(1 + datasetId.size() + q);
  k.push_back((char)(uint8_t)datasetId.size());
  k.append(datasetId);
  if (q) k.append((const char *)payload + questionOffset, q);
  return k;
}

// FNV-1a over the key, the map's hash. Equality is still on the full key
// bytes, so a collision costs a compare, never a wrong answer. The basis is
// mixed with a seed drawn once per cache, because every byte hashed is a
// byte the client chose: with a fixed seed a client can compute keys that
// share a bucket and turn each lookup into a scan of the whole loop's cache.
struct CacheKeyHash {
  uint64_t seed = 0;
  size_t operator()(const std::string &k) const {
    uint64_t h = 14695981039346656037ULL ^ seed;
    for (unsigned char c : k) {
      h ^= c;
      h *= 1099511628211ULL;
    }
    return (size_t)h;
  }
};

class ResultCache {
 public:
  explicit ResultCache(size_t capBytes = 0)
    : cap_(capBytes), map_(16, CacheKeyHash{SeedOf()}) {}

  // Per-entry bookkeeping beyond the columns, metadata and key bytes: the
  // list node, the map node and bucket slot, the shared_ptr control block
  // and the allocation headers of each. Estimated, not measured per entry.
  static constexpr size_t kEntryOverhead = 320;

  static size_t chargeOf(const std::string &key, const CacheEntry &e) {
    return e.bytes() + 2 * key.size() + kEntryOverhead;
  }

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
    size_t need = chargeOf(key, *entry);
    if (need > cap_) return;
    auto it = map_.find(key);
    if (it != map_.end()) {
      used_ -= chargeOf(it->second->key, *it->second->entry);
      order_.erase(it->second);
      map_.erase(it);
    }
    while (used_ + need > cap_ && !order_.empty()) {
      Node &victim = order_.back();
      used_ -= chargeOf(victim.key, *victim.entry);
      map_.erase(victim.key);
      order_.pop_back();
      evictions_++;
    }
    order_.push_front(Node{key, std::move(entry)});
    map_[order_.front().key] = order_.begin();
    used_ += need;
  }

 private:
  static uint64_t SeedOf() {
    std::random_device rd;
    return ((uint64_t)rd() << 32) ^ (uint64_t)rd();
  }
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
