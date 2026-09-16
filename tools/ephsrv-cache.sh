#!/bin/bash
# tools/ephsrv-cache.sh -- the ephemeris server's result-cache gate.
#
# Two halves. The first compiles a unit test against ephsrv/eph_cache.h
# alone -- no server, no Swiss -- and asserts the key canonicalization
# (delivery-only fields and the forced flags do not split entries; the
# fields that change the answer do) and the LRU mechanics (eviction order,
# byte accounting, the oversize refusal, a zero cap never hitting). The
# second drives a live astrolog-ephd on a scratch port with a 1 MiB cache
# and reads its --verbose log, which names every request "cache hit" or
# "cache miss": a repeated window is a hit AND bit-identical to the miss
# that filled it; an f32 request hits the f64 entry; a window bigger than
# the cap is computed, streamed and not stored; distinct windows evict the
# least recently used one and the cache stays under its cap; a server with
# --cache-mb 0 never hits.
#
# Knobs (env):
#   SWE_HOME   where the thread-safe fork lives  (default /shares/swisseph)
#   EPH        the ephemeris dir handed to --ephe (default: the repo's)
#   PORT       scratch port                      (default 47600 + pid % 300)
#
# Exit 0 with "CACHE PASS"; nonzero with the failed assertion. Falsified
# when written, four ways: with the server's cache.put() removed the live
# half fails at "repeated window must be a hit"; with precision added to
# the key, the oversize refusal removed, or the eviction loop disabled,
# the unit half fails first, by name, on the case that covers it. The
# live half was then confirmed to fail on the same three by running it
# with the unit half skipped.

set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
SWE_HOME=${SWE_HOME:-/shares/swisseph}
EPH=${EPH:-$ROOT/ephem}
PORT=${PORT:-$((47600 + $$ % 300))}
SCRATCH=$(mktemp -d /tmp/ephsrv-cache.XXXXXX)
EPHD_PID=
LOG="$SCRATCH/ephd.log"

cleanup() {
  [ -n "$EPHD_PID" ] && kill "$EPHD_PID" 2>/dev/null
  rm -rf "$SCRATCH"
}
trap cleanup EXIT

[ -x "$ROOT/astrolog-ephd" ] && [ -x "$ROOT/eph_wsclient" ] || make -s ephsrv >/dev/null

# ---- 1. The header on its own -------------------------------------------
cat > "$SCRATCH/unit.cpp" << 'EOF'
#include "eph_cache.h"
#include <cstdio>
#include <cstdlib>
using namespace eph;
static int fails = 0;
#define CHECK(cond, what) do { if (!(cond)) { printf("unit FAIL: %s\n", what); fails++; } } while (0)

static Request base() {
  Request r;
  ObjSpec a; a.kind = kObjBody; a.id = 0;
  ObjSpec b; b.kind = kObjBody; b.id = 1;
  r.objs = {a, b};
  r.iflag = 0;
  r.jdStart = 2451545.0;
  r.stepSeconds = 600;
  r.nTime = 10;
  r.precision = kPrecF64;
  r.chunkRows = 500;
  return r;
}

static std::shared_ptr<const CacheEntry> entryOf(size_t nDoubles) {
  auto e = std::make_shared<CacheEntry>();
  e->cols.assign(nDoubles, 1.0);
  return e;
}

int main() {
  // Key canonicalization.
  Request r0 = base();
  std::string k0 = cacheKeyOf(r0);
  { Request r = base(); r.precision = kPrecF32;
    CHECK(cacheKeyOf(r) == k0, "precision must not split the key"); }
  { Request r = base(); r.chunkRows = 7;
    CHECK(cacheKeyOf(r) == k0, "chunkRows must not split the key"); }
  { Request r = base(); r.iflag = EPH_CACHE_SEFLG_SWIEPH | EPH_CACHE_SEFLG_SPEED;
    CHECK(cacheKeyOf(r) == k0, "the forced flags must not split the key"); }
  { Request r = base(); r.sidMode = 1; r.sidT0 = 5.0; r.topoLat = 40.0;
    CHECK(cacheKeyOf(r) == k0, "sidereal/topo fields without their flag bits must not split the key"); }
  { Request r = base(); r.iflag = EPH_CACHE_SEFLG_SIDEREAL;
    std::string k1 = cacheKeyOf(r);
    CHECK(k1 != k0, "SEFLG_SIDEREAL must split the key");
    r.sidMode = 1;
    CHECK(cacheKeyOf(r) != k1, "sidMode under SEFLG_SIDEREAL must split the key"); }
  { Request r = base(); r.iflag = EPH_CACHE_SEFLG_TOPOCTR;
    std::string k1 = cacheKeyOf(r);
    r.topoLon = 1.0;
    CHECK(cacheKeyOf(r) != k1, "topo under SEFLG_TOPOCTR must split the key"); }
  { Request r = base(); r.jdStart += 1.0;
    CHECK(cacheKeyOf(r) != k0, "jdStart must split the key"); }
  { Request r = base(); r.stepSeconds = 601;
    CHECK(cacheKeyOf(r) != k0, "stepSeconds must split the key"); }
  { Request r = base(); r.nTime = 11;
    CHECK(cacheKeyOf(r) != k0, "nTime must split the key"); }
  { Request r = base(); r.center = 4;
    CHECK(cacheKeyOf(r) != k0, "center must split the key"); }
  { Request r = base(); std::swap(r.objs[0], r.objs[1]);
    CHECK(cacheKeyOf(r) != k0, "object order must split the key"); }
  { Request r = base(); r.objs[1].kind = kObjStar; snprintf(r.objs[1].name, sizeof(r.objs[1].name), "Sirius");
    std::string k1 = cacheKeyOf(r);
    CHECK(k1 != k0, "a star record must split the key");
    snprintf(r.objs[1].name, sizeof(r.objs[1].name), "Vega");
    CHECK(cacheKeyOf(r) != k1, "the star name must split the key"); }
  { Request r = base(); r.objs[1].kind = kObjNodAps; r.objs[1].point = kPntPerihelion; r.objs[1].method = kNodMean;
    std::string k1 = cacheKeyOf(r);
    CHECK(k1 != k0, "a node/apsis record must split the key");
    r.objs[1].point = kPntAphelion;
    std::string k2 = cacheKeyOf(r);
    CHECK(k2 != k1, "the node/apsis point must split the key");
    r.objs[1].method = kNodOscu;
    CHECK(cacheKeyOf(r) != k2, "the node/apsis method must split the key"); }
  { Request r = base(); r.iflag = kIflagTimeTT;
    CHECK(cacheKeyOf(r) != k0, "a TT instant is a different question from a UT one"); }
  { Request r = base(); r.iflag = kIflagCenter;
    CHECK(cacheKeyOf(r) != k0, "the center bit must split the key"); }

  // LRU mechanics: cap 100 bytes, entries of 40 (five doubles).
  ResultCache c(100);
  c.put("A", entryOf(5));
  c.put("B", entryOf(5));
  CHECK(c.usedBytes() == 80 && c.entries() == 2, "two entries account 80 bytes");
  CHECK(c.get("A") != nullptr, "A is present");
  c.put("C", entryOf(5));   // must evict B, the least recently used
  CHECK(c.get("B") == nullptr, "B was the LRU and must be evicted");
  CHECK(c.get("A") != nullptr, "A was touched and must survive");
  CHECK(c.get("C") != nullptr, "C was just inserted");
  CHECK(c.evictions() == 1, "exactly one eviction");
  CHECK(c.usedBytes() == 80 && c.entries() == 2, "accounting after eviction");
  CHECK(c.hits() == 3 && c.misses() == 1, "hit/miss counters: A, B(miss), A, C");
  c.put("D", entryOf(30));  // 240 bytes > cap: refused, nothing evicted
  CHECK(c.get("D") == nullptr, "an oversize entry is not stored");
  CHECK(c.entries() == 2 && c.evictions() == 1, "oversize refusal evicts nothing");
  c.put("A", entryOf(2));   // replace: 16 bytes now
  CHECK(c.entries() == 2 && c.usedBytes() == 56, "replacing a key re-accounts it");
  auto held = c.get("C");
  c.put("E", entryOf(5)); c.put("F", entryOf(5)); c.put("G", entryOf(5));
  CHECK(held && held->cols.size() == 5, "a held entry survives its eviction");
  CHECK(c.usedBytes() <= 100, "never over the cap");

  ResultCache z(0);
  z.put("A", entryOf(5));
  CHECK(z.get("A") == nullptr && z.entries() == 0, "zero cap stores nothing");
  CHECK(z.hits() == 0 && z.misses() == 0, "zero cap counts nothing");

  if (fails) return 1;
  printf("unit: ok\n");
  return 0;
}
EOF
g++ -std=gnu++17 -O2 -Wall -I ephsrv "$SCRATCH/unit.cpp" -o "$SCRATCH/unit"
"$SCRATCH/unit" || { echo "CACHE FAIL: unit test"; exit 1; }

# ---- 2. The live server -------------------------------------------------
start_server() {   # $1 = --cache-mb
  "$ROOT/astrolog-ephd" --port "$PORT" --ephe "$EPH" --threads 1 --verbose \
    --cache-mb "$1" > "$LOG" 2>&1 &
  EPHD_PID=$!
  for i in $(seq 1 50); do
    grep -q "listening on port" "$LOG" 2>/dev/null && break
    sleep 0.1
  done
  grep -q "listening on port" "$LOG" || { echo "CACHE FAIL: server did not start"; exit 1; }
}
stop_server() {
  kill "$EPHD_PID" 2>/dev/null; wait "$EPHD_PID" 2>/dev/null || true
  EPHD_PID=
}
# The last request line of the log, i.e. the request the client just made.
last_line() { grep "ephd: request" "$LOG" | tail -1; }
ask() {   # ask <out> <extra client args...>
  local out=$1; shift
  "$ROOT/eph_wsclient" --port "$PORT" --out "$out" --quiet "$@" \
    || { echo "CACHE FAIL: request failed: $*"; exit 1; }
}
expect() {   # expect <substring> <what>
  local line; line=$(last_line)
  case "$line" in
    *"$1"*) ;;
    *) echo "CACHE FAIL: $2"; echo "  log: $line"; exit 1 ;;
  esac
}
# "cache N entries K.K KiB" -> the KiB figure of the last line.
kib_of() { last_line | sed -E 's/.*cache [0-9]+ entries ([0-9.]+) KiB.*/\1/'; }
entries_of() { last_line | sed -E 's/.*cache ([0-9]+) entries.*/\1/'; }
evictions_of() { last_line | sed -E 's/.*misses ([0-9]+) evictions.*/\1/'; }

start_server 1

# a. A window that fits (10 bodies x 500 rows = 240 KiB): miss, then hit,
#    bit-identical.
TEN="0,1,2,3,4,5,6,7,8,9"
ask "$SCRATCH/m.txt" --objs "$TEN" --jd 2451545.0 --step 600 --count 500
expect "cache miss" "first window must be a miss"
ask "$SCRATCH/h.txt" --objs "$TEN" --jd 2451545.0 --step 600 --count 500
expect "cache hit" "repeated window must be a hit"
cmp -s "$SCRATCH/m.txt" "$SCRATCH/h.txt" \
  || { echo "CACHE FAIL: the hit's columns differ from the miss that filled it"; exit 1; }
echo "hit: bit-identical to the miss that filled it"

# b. The same window at f32 is a hit on the f64 entry: precision is a
#    delivery parameter.
ask "$SCRATCH/f.txt" --objs "$TEN" --jd 2451545.0 --step 600 --count 500 --precision 32
expect "cache hit" "an f32 request must hit the f64 entry"
# and chunkRows is too: the client always sends kMaxChunkRows, so this is
# covered by the unit test rather than the wire.
echo "f32: hit on the f64 entry"

# c. A window larger than the cap (30 bodies x 1000 rows = 1.4 MiB > 1 MiB)
#    is answered but not stored: entry count unchanged, second ask misses.
THIRTY="0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,10005,10006,10007,10008,10009,10010,10011"
before=$(entries_of)
ask "$SCRATCH/big1.txt" --objs "$THIRTY" --jd 2451545.0 --step 600 --count 1000
expect "cache miss" "oversize window must miss"
[ "$(entries_of)" = "$before" ] \
  || { echo "CACHE FAIL: oversize window was stored ($(entries_of) entries, had $before)"; exit 1; }
ask "$SCRATCH/big2.txt" --objs "$THIRTY" --jd 2451545.0 --step 600 --count 1000
expect "cache miss" "oversize window must miss again (not stored)"
[ "$(evictions_of)" = "0" ] \
  || { echo "CACHE FAIL: oversize window evicted something"; exit 1; }
echo "oversize: answered, not stored, nothing evicted"

# d. Distinct 240 KiB windows into a 1024 KiB cache, which holds four.
#    jd+0 is in already; insert jd+1..3 (misses), then TOUCH jd+0 so it is
#    the most recently used, then insert jd+4: the fifth entry must evict
#    the least recently used, which is now jd+1 -- a FIFO would evict jd+0
#    instead, and that is the difference this asserts. The cache never
#    exceeds its cap.
for d in 1 2 3; do
  ask "$SCRATCH/w$d.txt" --objs "$TEN" --jd "$((2451545 + d)).0" --step 600 --count 500
  expect "cache miss" "distinct window $d must miss"
done
ask "$SCRATCH/t.txt" --objs "$TEN" --jd 2451545.0 --step 600 --count 500
expect "cache hit" "jd+0 must still be present with four windows in"
ask "$SCRATCH/w4.txt" --objs "$TEN" --jd 2451549.0 --step 600 --count 500
expect "cache miss" "distinct window 4 must miss"
ev=$(evictions_of)
[ "$ev" -ge 1 ] || { echo "CACHE FAIL: five windows in a four-window cache evicted nothing"; exit 1; }
kib=$(kib_of)
awk -v k="$kib" 'BEGIN { exit !(k <= 1024) }' \
  || { echo "CACHE FAIL: cache holds $kib KiB, over its 1024 KiB cap"; exit 1; }
ask "$SCRATCH/x.txt" --objs "$TEN" --jd 2451546.0 --step 600 --count 500
expect "cache miss" "the least recently used window (jd+1) must have been evicted"
ask "$SCRATCH/y.txt" --objs "$TEN" --jd 2451545.0 --step 600 --count 500
expect "cache hit" "the touched window (jd+0) must have survived; eviction is LRU, not FIFO"
ask "$SCRATCH/y2.txt" --objs "$TEN" --jd 2451549.0 --step 600 --count 500
expect "cache hit" "the most recent window must still be present"
echo "eviction: LRU order, $kib KiB under the 1024 KiB cap"

stop_server

# e. --cache-mb 0: nothing is ever a hit.
start_server 0
ask "$SCRATCH/z1.txt" --objs "$TEN" --jd 2451545.0 --step 600 --count 500
ask "$SCRATCH/z2.txt" --objs "$TEN" --jd 2451545.0 --step 600 --count 500
expect "cache miss" "with --cache-mb 0 a repeated window must miss"
cmp -s "$SCRATCH/z1.txt" "$SCRATCH/z2.txt" \
  || { echo "CACHE FAIL: two uncached computations of one window differ"; exit 1; }
stop_server
echo "disabled: --cache-mb 0 never hits"

echo "CACHE PASS: unit + live (hit, f32, oversize, eviction, disabled)"
