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
#   PORT       scratch port                      (default 28400 + pid % 300)
#
# Exit 0 with "CACHE PASS"; nonzero with the failed assertion.
#
#   tools/ephsrv-cache.sh --selftest
#
# runs section 2a instead: the GATE's own decisions, against crafted log
# lines, with no server and no compiler, in about a second. A control plus
# eight injections, each required to red its own assertion.
#
# It exists because the paragraph that used to stand here -- "falsified when
# written, four ways", with the four spelled out -- was a measurement taken
# once, in 2026-09-17, by hand, and nothing ran those injections again. Those
# four are still true and still worth knowing: with the server's cache.put()
# removed the live half fails at "repeated window must be a hit"; with
# precision added to the key, the oversize refusal removed, or the eviction
# loop disabled, the unit half fails first, by name, on the case that covers
# it. What is new is that a hole the author did not think of is now findable,
# and the first run found two:
#
#   - kib_of/entries_of/evictions_of were bare `sed s/.../\1/`, and sed
#     prints the line UNCHANGED when it does not match, so a log that stopped
#     carrying a field handed the whole line downstream as though it were a
#     number. Measured: the KiB and eviction checks failed anyway, but the
#     oversize check compares two such lines as TEXT and is unequal only
#     because their timestamps differ. Safe by luck, not by construction.
#   - expect() graded `grep evt=req | tail -1` without requiring that a new
#     line had appeared, so a request the server never logged was graded on
#     the PREVIOUS request's line -- green, about the wrong request.

set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
SELFTEST=0
[ "${1:-}" = "--selftest" ] && SELFTEST=1 || true
SWE_HOME=${SWE_HOME:-/shares/swisseph}
EPH=${EPH:-$ROOT/ephem}
PORT=${PORT:-$((28400 + $$ % 300))}   # below the ephemeral range: ephsrv-robust.sh says why
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
// eph_cache.h alone -- no server, no Swiss: the key protocol version 4
// defines (3.7) and the LRU mechanics.
#include "eph_cache.h"
#include <cstdio>
#include <cstdlib>
using namespace eph;
static int fails = 0;
#define CHECK(cond, what) do { if (!(cond)) { printf("unit FAIL: %s\n", what); fails++; } } while (0)

static const char kDataset[] = "astrolog-ephd 2.0/swiss 2.10.03/files#deadbeef";

static Request base() {
  Request r;
  r.profiles.push_back(Profile());
  Object a, b;
  a.kind = kObjBody; a.naif = 10;
  b.kind = kObjBody; b.naif = 301;
  r.objs = {a, b};
  r.start = Time{2451545.0, 0.0};
  r.stepNs = 600LL * 1000000000LL;
  r.nTime = 10;
  r.precision = kPrecF64;
  r.chunkRows = 500;
  return r;
}

// The key the server computes: encode the request, parse it back (which is
// what says where the question block starts), then key on it.
static std::string keyOf(const Request &r, const char *dataset = kDataset) {
  std::vector<uint8_t> pay;
  EncodeRequest(&pay, r);
  Request back;
  std::string why;
  if (ParseRequest(pay.data(), pay.size(), &back, &why) != kOk) {
    printf("unit FAIL: a request this test built does not parse: %s\n", why.c_str());
    fails++;
    return std::string();
  }
  return CacheKey(dataset, pay.data(), pay.size(), back.questionOffset);
}

static std::shared_ptr<const CacheEntry> entryOf(size_t nDoubles) {
  auto e = std::make_shared<CacheEntry>();
  e->cols.assign(nDoubles, 1.0);
  return e;
}

int main() {
  // The key: delivery out, question in (3.7).
  Request r0 = base();
  std::string k0 = keyOf(r0);
  CHECK(!k0.empty(), "the base request keys");
  { Request r = base(); r.precision = kPrecF32;
    CHECK(keyOf(r) == k0, "precision must not split the key"); }
  { Request r = base(); r.chunkRows = 7;
    CHECK(keyOf(r) == k0, "chunkRows must not split the key"); }
  { Request r = base(); r.priority = 1;
    CHECK(keyOf(r) == k0, "priority must not split the key"); }
  { CHECK(keyOf(r0, "another dataset") != k0,
          "the datasetId must split the key: another dataset is another answer"); }
  { Request r = base(); r.start.jd2 += 1.0;
    CHECK(keyOf(r) != k0, "the first instant must split the key"); }
  { Request r = base(); r.stepNs += 1000;
    CHECK(keyOf(r) != k0, "the step must split the key"); }
  { Request r = base(); r.nTime = 11;
    CHECK(keyOf(r) != k0, "the row count must split the key"); }
  { Request r = base(); r.timeScale = kTimeUT1;
    CHECK(keyOf(r) != k0, "the time scale must split the key"); }
  { Request r = base(); r.deltaTSec = 69.2;
    CHECK(keyOf(r) != k0, "a given delta T must split the key"); }
  { Request r = base(); std::swap(r.objs[0], r.objs[1]);
    CHECK(keyOf(r) != k0, "object order must split the key"); }
  { Request r = base(); r.objs[1].kind = kObjStar; r.objs[1].name = "Sirius";
    std::string k1 = keyOf(r);
    CHECK(k1 != k0, "a star record must split the key");
    r.objs[1].name = "Vega";
    CHECK(keyOf(r) != k1, "the star name must split the key"); }
  { Request r = base(); r.objs[1].kind = kObjOrbitPoint; r.objs[1].naif = 301;
    r.objs[1].point = kPtPeri; r.objs[1].method = kMethMean;
    std::string k1 = keyOf(r);
    CHECK(k1 != k0, "an orbit point must split the key");
    r.objs[1].point = kPtApo;
    std::string k2 = keyOf(r);
    CHECK(k2 != k1, "its point must split the key");
    r.objs[1].method = kMethOsculating;
    CHECK(keyOf(r) != k2, "and its method"); }
  { Request r = base(); r.profiles[0].observer = kObsHelio;
    CHECK(keyOf(r) != k0, "the observer must split the key"); }
  { Request r = base(); r.profiles[0].corrections = kCorrLightTime;
    CHECK(keyOf(r) != k0, "the correction mask must split the key"); }
  { Request r = base(); r.profiles[0].zodiac = "lahiri";
    std::string k1 = keyOf(r);
    CHECK(k1 != k0, "a sidereal zodiac must split the key");
    r.profiles[0].siderealPlane = kSidPlaneInvariable;
    CHECK(keyOf(r) != k1, "and its plane"); }
  { Request r = base(); r.profiles[0].columns = kColAyanamsa;
    CHECK(keyOf(r) != k0, "an extra column must split the key"); }
  { Request r = base();
    Tlv e;
    EncodeDeltaTTable({{{2451545.0, 0.0}, 60.0}, {{2451547.0, 0.0}, 64.0}}, &e);
    r.ext.push_back(e);
    CHECK(keyOf(r) != k0, "a delta T table must split the key"); }

  // LRU mechanics, in units of what one five-double entry under a
  // one-letter key is charged -- the columns, the key twice and the
  // per-entry allowance (ResultCache::chargeOf) -- with room for two and a
  // half of them. The first draft counted columns only, and the cache
  // really held about three times its cap in chart-sized entries.
  const size_t U = ResultCache::chargeOf("A", *entryOf(5));
  CHECK(U == 40 + 2 + ResultCache::kEntryOverhead, "an entry is charged columns, key twice and the allowance");
  ResultCache c(U * 5 / 2);
  c.put("A", entryOf(5));
  c.put("B", entryOf(5));
  CHECK(c.usedBytes() == 2 * U && c.entries() == 2, "two entries account two charges");
  CHECK(c.get("A") != nullptr, "A is present");
  c.put("C", entryOf(5));   // must evict B, the least recently used
  CHECK(c.get("B") == nullptr, "B was the LRU and must be evicted");
  CHECK(c.get("A") != nullptr, "A was touched and must survive");
  CHECK(c.get("C") != nullptr, "C was just inserted");
  CHECK(c.evictions() == 1, "exactly one eviction");
  CHECK(c.usedBytes() == 2 * U && c.entries() == 2, "accounting after eviction");
  CHECK(c.hits() == 3 && c.misses() == 1, "hit/miss counters: A, B(miss), A, C");
  c.put("D", entryOf(U * 3 / 8));  // over the cap alone: refused, nothing evicted
  CHECK(c.get("D") == nullptr, "an oversize entry is not stored");
  CHECK(c.entries() == 2 && c.evictions() == 1, "oversize refusal evicts nothing");
  c.put("A", entryOf(2));   // replace with a smaller entry
  CHECK(c.entries() == 2 && c.usedBytes() == U + ResultCache::chargeOf("A", *entryOf(2)),
        "replacing a key re-accounts it");
  auto held = c.get("C");
  c.put("E", entryOf(5)); c.put("F", entryOf(5)); c.put("G", entryOf(5));
  CHECK(held && held->cols.size() == 5, "a held entry survives its eviction");
  CHECK(c.usedBytes() <= U * 5 / 2, "never over the cap");

  // An entry's metadata is charged too: its strings are the object names
  // and the first failure's text, which a chart-sized answer carries 30 of.
  {
    auto e = std::make_shared<CacheEntry>();
    e->cols.assign(5, 1.0);
    e->meta.resize(1);
    e->meta[0].name = "Mercury";
    e->meta[0].errText = "the instant is outside this ephemeris's coverage";
    CHECK(ResultCache::chargeOf("A", *e) > U, "metadata is charged with the columns");
  }

  ResultCache z(0);
  z.put("A", entryOf(5));
  CHECK(z.get("A") == nullptr && z.entries() == 0, "zero cap stores nothing");
  CHECK(z.hits() == 0 && z.misses() == 0, "zero cap counts nothing");

  if (fails) return 1;
  printf("unit: ok\n");
  return 0;
}
EOF
if [ "$SELFTEST" = 0 ]; then
  g++ -std=gnu++17 -O2 -Wall -I ephsrv "$SCRATCH/unit.cpp" -o "$SCRATCH/unit"
  "$SCRATCH/unit" || { echo "CACHE FAIL: unit test"; exit 1; }
fi

# ---- 2. The live server -------------------------------------------------
start_server() {   # $1 = --cache-mb
  # No compute budget: this gate asks the same windows over and over on
  # purpose, which is exactly what --cells-per-sec is there to refuse.
  "$ROOT/astrolog-ephd" --port "$PORT" --ephe "$EPH" --threads 1 --verbose \
    --cells-per-sec 0 --cache-mb "$1" > "$LOG" 2>&1 &
  EPHD_PID=$!
  for i in $(seq 1 50); do
    grep -q "evt=listen port=" "$LOG" 2>/dev/null && break
    sleep 0.1
  done
  grep -q "evt=listen port=" "$LOG" || { echo "CACHE FAIL: server did not start"; exit 1; }
  nSeen=0   # a new server truncates $LOG, so the lines seen so far are gone
}
stop_server() {
  kill "$EPHD_PID" 2>/dev/null; wait "$EPHD_PID" 2>/dev/null || true
  EPHD_PID=
}
# The last request line of the log, i.e. the request the client just made.
# Safe against a race by construction and not by luck: the server writes a
# request's line BEFORE its last chunk (eph_srv.cpp, "the request's line goes
# out BEFORE its last chunk"), and every line is one fwrite + fflush under
# flockfile, so a line a client could have caused is in the FILE, not in a
# buffer, by the time the client returns.
last_line() { grep " evt=req " "$LOG" | tail -1; }
# ...but "the last line" is only the line of the request just made if a line
# was actually added. A request that produced none -- it errored, it was
# refused, the format moved -- leaves the PREVIOUS request's line there, and
# every expect below would then grade the wrong request and could pass on it.
# So the count has to move too. nSeen is per-server: start_server resets it.
nSeen=0
reqs_logged() { grep -c " evt=req " "$LOG" 2>/dev/null || true; }
ask() {   # ask <out> <extra client args...>
  local out=$1; shift
  "$ROOT/eph_wsclient" --port "$PORT" --out "$out" --quiet "$@" \
    || { echo "CACHE FAIL: request failed: $*"; exit 1; }
}
expect() {   # expect <substring> <what>
  local line n; n=$(reqs_logged)
  if [ "$n" -le "$nSeen" ]; then
    echo "CACHE FAIL: $2"
    echo "  the request logged no line of its own ($n lines, had $nSeen), so this"
    echo "  would have graded the PREVIOUS request. Nothing about the cache is"
    echo "  proven either way."
    exit 1
  fi
  nSeen=$n
  line=$(last_line)
  case "$line" in
    *"$1"*) ;;
    *) echo "CACHE FAIL: $2"; echo "  log: $line"; exit 1 ;;
  esac
}
# The cache's state after the last request, from that request's line.
#
# A MISSING FIELD IS A FAILURE, not a value. These were three bare
# substitutions -- `sed -E 's/.* cache_kib=([0-9.]+).*/\1/'` -- and sed prints
# the line UNCHANGED when the pattern does not match, so a log that stopped
# carrying the field handed the whole log line to the caller as though it were
# a number. What each caller then did with it was luck, and measured
# (2026-09-20): the KiB cap compared a string against 1024 in awk and failed;
# the eviction count hit bash's "integer expression expected" and failed; but
# the oversize check is `[ "$(entries_of)" = "$before" ]`, two whole log lines
# compared as text, and those are UNEQUAL only because their timestamps
# differ. Take the timestamps away and a gate whose field vanished reports
# "oversize window was not stored" having measured nothing at all.
field_of() {   # field_of <key>
  local line v; line=$(last_line)
  v=$(printf '%s\n' "$line" | sed -nE "s/.* $1=([0-9.]+).*/\1/p")
  [ -n "$v" ] || {
    echo "CACHE FAIL: the log line carries no $1= field, so nothing downstream"
    echo "  of it is measuring what it says it measures."
    echo "  log: $line"
    exit 1
  }
  printf '%s\n' "$v"
}
kib_of() { field_of cache_kib; }
entries_of() { field_of cache_entries; }
evictions_of() { field_of cache_evictions; }

# Is <f32file> exactly <f64file> rounded to f32, column for column? ONE copy,
# called by the live half and by --selftest, so what the selftest falsifies is
# what the gate runs rather than a duplicate of it that can drift.
f32_is_rounded_copy() {   # f32_is_rounded_copy <f64file> <f32file>
  python3 - "$1" "$2" << 'PYEOF'
import struct, sys
a = open(sys.argv[1]).read().split('\n'); b = open(sys.argv[2]).read().split('\n')
assert len(a) == len(b) and len(a) > 1, "row counts differ"
for la, lb in zip(a, b):
    if not la: continue
    fa, fb = la.split(), lb.split()
    for x, y in zip(fa[3:], fb[3:]):
        f32 = struct.unpack('f', struct.pack('f', float.fromhex(x)))[0]
        assert float.fromhex(y) == f32, (la, lb)
PYEOF
}

# ---- 2a. --selftest: the gate's own decisions, falsified -----------------
#
# Everything above this line grades a SERVER. This grades the GATE, and it is
# here because the header's "falsified when written, four ways" is a
# measurement someone took once, in 2026-09-17, by hand, and nothing has run
# those four injections since. A falsification recorded as prose is not a
# check.
#
# It runs no server and compiles nothing: it points $LOG at crafted lines and
# calls THE SAME expect/field_of/f32_is_rounded_copy the live half calls, so
# what is falsified is what runs. Seconds, no ephemeris, no network.
#
# A CONTROL comes first and it is load-bearing, not decoration. An assertion
# that is red unconditionally satisfies every injection while proving nothing
# -- that exact mistake was made in ephsrv-soak.sh's selftest, where two
# sabotages both "correctly" reddened a check that had been made to fire
# always, and only the control caught it.
if [ "$SELFTEST" = 1 ]; then
  st_fail=0
  st_ran=0
  # want=ok: the command must succeed. want=red: it must fail, and failing is
  # the whole point, so a case that "passes" by erroring for some unrelated
  # reason is indistinguishable here -- which is why each case below changes
  # exactly ONE thing away from the control.
  st_case() {   # st_case <want ok|red> <desc> <command...>
    local want=$1 desc=$2; shift 2
    st_ran=$((st_ran + 1))
    # A SUBSHELL, because expect() and field_of() report by calling exit: run
    # straight they would end this script, and the first injection did exactly
    # that -- the selftest stopped after its control, looking like a pass with
    # one case in it.
    if ( "$@" ) > /dev/null 2>&1; then
      [ "$want" = ok ] || { echo "  SELFTEST FAIL: $desc -- passed, must have failed"; st_fail=$((st_fail + 1)); return; }
    else
      [ "$want" = red ] || { echo "  SELFTEST FAIL: $desc -- failed, must have passed"; st_fail=$((st_fail + 1)); return; }
    fi
    echo "  ok: $desc"
  }
  ST=$SCRATCH/st; mkdir -p "$ST"

  # One well-formed request line, and a second that differs only in the one
  # field each case is about.
  GOOD='ts=2026-09-20T10:00:00.000Z level=info evt=req conn=1 req=1 objs=10 rows=500 cache=hit cache_kib=240.0 cache_entries=1 cache_evictions=0'
  MISS=${GOOD/cache=hit/cache=miss}

  echo "== --selftest: the gate's own decisions"

  # -- expect(): the control, then the two ways it must go red.
  printf '%s\n' "$GOOD" > "$ST/log"; LOG=$ST/log; nSeen=0
  st_case ok  "control: expect hit on a hit line"      expect "cache=hit" x
  printf '%s\n' "$MISS" > "$ST/log"; nSeen=0
  st_case red "expect hit on a MISS line"              expect "cache=hit" x
  # The stale-line case: the log did not grow, so the last line belongs to an
  # earlier request. Before nSeen existed this passed, grading the wrong
  # request -- a gate reporting "repeated window must be a hit" about a
  # request the server never logged.
  printf '%s\n' "$GOOD" > "$ST/log"; nSeen=1
  st_case red "expect on a log that did not grow (stale line)"  expect "cache=hit" x
  : > "$ST/log"; nSeen=0
  st_case red "expect against a log with no request lines"      expect "cache=hit" x

  # -- field_of(): the control, then the missing field. This is the one that
  #    was silently returning the whole log line as if it were a number.
  printf '%s\n' "$GOOD" > "$ST/log"
  st_case ok  "control: field_of finds cache_entries"  field_of cache_entries
  [ "$(field_of cache_entries)" = 1 ] || { echo "  SELFTEST FAIL: field_of read the wrong value"; st_fail=$((st_fail + 1)); }
  st_ran=$((st_ran + 1)); echo "  ok: field_of reads the value, not just any digits"
  printf '%s\n' "${GOOD/ cache_entries=1/}" > "$ST/log"
  st_case red "field_of on a line with no cache_entries="        field_of cache_entries
  printf '%s\n' "${GOOD/ cache_kib=240.0/}" > "$ST/log"
  st_case red "field_of on a line with no cache_kib="            field_of cache_kib

  # -- f32_is_rounded_copy(): the control, then a value that is NOT the f64
  #    rounded, then a row-count difference. The client's dump is
  #    "idx label err ..." and the comparison starts at field 3.
  printf 'r0 a 0 %s\n' "$(python3 -c 'print(float.hex(1.2345678901234))')" > "$ST/f64"
  printf 'r0 a 0 %s\n' "$(python3 -c 'import struct;print(float.hex(struct.unpack("f",struct.pack("f",1.2345678901234))[0]))')" > "$ST/f32"
  st_case ok  "control: an exactly-rounded f32 copy"   f32_is_rounded_copy "$ST/f64" "$ST/f32"
  printf 'r0 a 0 %s\n' "$(python3 -c 'print(float.hex(1.2345678901234))')" > "$ST/f32bad"
  st_case red "an f32 column that is the f64, unrounded"         f32_is_rounded_copy "$ST/f64" "$ST/f32bad"
  printf 'r0 a 0 %s\nr1 a 0 %s\n' "$(python3 -c 'print(float.hex(1.0))')" "$(python3 -c 'print(float.hex(2.0))')" > "$ST/f32long"
  st_case red "an f32 file with a different row count"           f32_is_rounded_copy "$ST/f64" "$ST/f32long"

  # The count, for the reason golden now asserts its own (work-log item 27):
  # a case that stops running takes its own assertion with it and leaves a
  # smaller number nobody reads.
  [ "$st_ran" -eq 11 ] || { echo "SELFTEST FAIL: $st_ran cases ran, expected 11"; exit 1; }
  [ "$st_fail" = 0 ] || { echo "SELFTEST FAIL: $st_fail of $st_ran cases"; exit 1; }
  echo "CACHE SELFTEST PASS: $st_ran cases (1 control + 8 injections + 2 value reads)"
  exit 0
fi

start_server 1

# a. A window that fits (10 bodies x 500 rows = 240 KiB): miss, then hit,
#    bit-identical.
TEN="10,301,199,299,4,5,6,7,8,9"
ask "$SCRATCH/m.txt" --objs "$TEN" --jd 2451545.0 --step 600 --count 500
expect "cache=miss" "first window must be a miss"
ask "$SCRATCH/h.txt" --objs "$TEN" --jd 2451545.0 --step 600 --count 500
expect "cache=hit" "repeated window must be a hit"
cmp -s "$SCRATCH/m.txt" "$SCRATCH/h.txt" \
  || { echo "CACHE FAIL: the hit's columns differ from the miss that filled it"; exit 1; }
echo "hit: bit-identical to the miss that filled it"

# b. The same window at f32 is a hit on the f64 entry: precision is a
#    delivery parameter.
ask "$SCRATCH/f.txt" --objs "$TEN" --jd 2451545.0 --step 600 --count 500 --precision 32
expect "cache=hit" "an f32 request must hit the f64 entry"
# and chunkRows is too: the client always sends kMaxChunkRows, so this is
# covered by the unit test rather than the wire.
# And the values are the f64 entry's, rounded: the log word alone said
# nothing about what came back (EPHEMERIS_REVIEW.md T13).
f32_is_rounded_copy "$SCRATCH/m.txt" "$SCRATCH/f.txt" \
  || { echo "CACHE FAIL: the f32 hit's values are not the f64 entry's"; exit 1; }
echo "f32: hit on the f64 entry, and its values are the entry's rounded to f32"

# c. A window larger than the cap (30 bodies x 1000 rows = 1.4 MiB > 1 MiB)
#    is answered but not stored: entry count unchanged, second ask misses.
THIRTY="10,301,199,299,4,5,6,7,8,9,399,20000001,20000002,20000003,20000004,20002060,20005145,20000005,20000006,20000007,20000008,20000009,20000010,20000011,20000012,20000013,20000014,20000015,20000016,20000017"
before=$(entries_of)
ask "$SCRATCH/big1.txt" --objs "$THIRTY" --jd 2451545.0 --step 600 --count 1000
expect "cache=miss" "oversize window must miss"
[ "$(entries_of)" = "$before" ] \
  || { echo "CACHE FAIL: oversize window was stored ($(entries_of) entries, had $before)"; exit 1; }
ask "$SCRATCH/big2.txt" --objs "$THIRTY" --jd 2451545.0 --step 600 --count 1000
expect "cache=miss" "oversize window must miss again (not stored)"
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
  expect "cache=miss" "distinct window $d must miss"
done
ask "$SCRATCH/t.txt" --objs "$TEN" --jd 2451545.0 --step 600 --count 500
expect "cache=hit" "jd+0 must still be present with four windows in"
ask "$SCRATCH/w4.txt" --objs "$TEN" --jd 2451549.0 --step 600 --count 500
expect "cache=miss" "distinct window 4 must miss"
ev=$(evictions_of)
[ "$ev" -ge 1 ] || { echo "CACHE FAIL: five windows in a four-window cache evicted nothing"; exit 1; }
kib=$(kib_of)
awk -v k="$kib" 'BEGIN { exit !(k <= 1024) }' \
  || { echo "CACHE FAIL: cache holds $kib KiB, over its 1024 KiB cap"; exit 1; }
ask "$SCRATCH/x.txt" --objs "$TEN" --jd 2451546.0 --step 600 --count 500
expect "cache=miss" "the least recently used window (jd+1) must have been evicted"
ask "$SCRATCH/y.txt" --objs "$TEN" --jd 2451545.0 --step 600 --count 500
expect "cache=hit" "the touched window (jd+0) must have survived; eviction is LRU, not FIFO"
ask "$SCRATCH/y2.txt" --objs "$TEN" --jd 2451549.0 --step 600 --count 500
expect "cache=hit" "the most recent window must still be present"
echo "eviction: LRU order, $kib KiB under the 1024 KiB cap"

stop_server

# e. --cache-mb 0: nothing is ever a hit.
start_server 0
ask "$SCRATCH/z1.txt" --objs "$TEN" --jd 2451545.0 --step 600 --count 500
ask "$SCRATCH/z2.txt" --objs "$TEN" --jd 2451545.0 --step 600 --count 500
expect "cache=miss" "with --cache-mb 0 a repeated window must miss"
cmp -s "$SCRATCH/z1.txt" "$SCRATCH/z2.txt" \
  || { echo "CACHE FAIL: two uncached computations of one window differ"; exit 1; }
stop_server
echo "disabled: --cache-mb 0 never hits"

echo "CACHE PASS: unit + live (hit, f32, oversize, eviction, disabled)"
