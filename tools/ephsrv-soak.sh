#!/bin/bash
# tools/ephsrv-soak.sh -- the ephemeris server's million-file gate.
#
# Builds a synthetic numbered-asteroid farm (ast0/..../astN/, the layout
# swi_gen_filename() looks for: ast<id/1000>/se<id:05d>.se1), points
# astrolog-ephd at it, and asserts the EPHEMERIS_SERVER_PLAN.md §7
# invariants that a nearly-million-file tree demands:
#   a. startup is O(1): wall time under STARTUP_MAX seconds, farm or no farm;
#   b. startup performs ZERO directory scans: strace sees no opendir and no
#      getdents* before the "ephemeris path" log line;
#   c. open fds are bounded by the context pool, not the tree: the server's
#      /proc fd count is stable across a barrage of requests spread over
#      the farm (FD_TOL slack);
#   d. an asteroid whose file is absent comes back as a clean per-object
#      failure -- no rows, an A.17 code and its text -- without killing the
#      connection;
#   e. RSS is bounded by --cache-mb AND the cache is really filling -- two
#      assertions, because a bound alone is green on a server that caches
#      nothing.
#
# Knobs (env):
#   FARM_N      asteroid count to synthesize      (default 100000)
#   STARTUP_MAX startup wall-time bound, seconds  (default 2)
#   FD_TOL      allowed fd growth over the barrage(default 20)
#   BARRAGE     requests in the fd barrage         (default 200)
#   MEM_CAP     cache cap for the memory leg, MB   (default 8)
#   MEM_SABOTAGE  "unbounded" or "nocache" -- fault injection for --selftest
#   MEM_N       distinct windows it asks for       (default 300)
#   KEEP_FARM   set to keep the farm directory
#
# Exit 0 with "SOAK PASS"; nonzero with the failed assertion. Re-runnable;
# cleans up the farm unless KEEP_FARM is set.
#
# "--selftest" covers LEG (e) ONLY, and says so because the scope is the
# part a selftest lies about. It runs a control and one fault per
# assertion leg (e) makes, requires the RIGHT one to red, and refuses to
# start if leg (e) can fail in a way no case covers. Legs a-d still carry
# their falsification as prose, like the other nine ephsrv-*.sh gates.
# About four minutes.

set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD

# --selftest: run the memory leg with each fault injected and require the
# RIGHT assertion to red. This exists because every gate in this project
# documents its own falsification in PROSE -- "falsified when written,
# four ways" -- which is a measurement taken once and never taken again.
# tools/ci-selftest.sh already does this for the ci-*.sh family; no
# ephsrv-*.sh gate was in it. The precedent for the flag itself is
# image_audit.py --selftest.
if [ "${1:-}" = "--selftest" ]; then
  fails=0
  # THE CASE LIST IS CHECKED AGAINST THE LEG, before anything is run.
  #
  # A selftest declares which assertions it falsifies, and that claim rots
  # the moment an assertion is added without a case -- the assertion could
  # then be DELETED and every case would still go green, which is the
  # exact overclaim this flag exists to prevent, in the flag itself. The
  # Prometheia project found precisely that in their own selftest hours
  # after writing it: nine assertions declared, seven exercised.
  #
  # So the list is read out of leg (e)'s own SOAK FAIL messages rather
  # than kept by hand beside them, and a message no case claims stops
  # this before a daemon is started. Add an assertion, get told to add a
  # case.
  SELF_CASES="bounded filling plateaued"
  SELF_LOG=$(mktemp /tmp/ephsrv-soak-asserts.XXXXXX)
  # The CONTROL first, and it is not ceremony. The two sabotages below
  # only ever prove the gate can go red; a leg that failed unconditionally
  # -- a bound mistyped to something nothing can satisfy, a daemon that
  # stopped starting -- would satisfy both of them and be useless. This is
  # the "these agree" row of the pair, and here it is the one that carries
  # the weight, which is the reverse of the usual way round.
  if FARM_N=200 MEM_N=200 MEM_ASSERT_LOG="$SELF_LOG" "$0" > /dev/null 2>&1; then
    echo "selftest: the control passes"
  else
    echo "SELFTEST FAIL: the gate does not pass with nothing injected --"
    echo "               the sabotages below would prove nothing"
    fails=$((fails+1))
  fi
  # THE CASE LIST, CHECKED AGAINST WHAT THE LEG ACTUALLY EVALUATED.
  #
  # The control passes, so every assertion in leg (e) ran and recorded
  # itself. An assertion added without a case appears here and has no
  # case below; a case for an assertion that no longer exists appears
  # below with nothing here. Either stops the selftest, because a
  # selftest that silently covers less than it claims is the thing this
  # flag exists to prevent -- and the Prometheia project found exactly
  # that in their own, hours after writing it: nine declared, seven run.
  RAN=$(sort -u "$SELF_LOG" 2>/dev/null | tr '\n' ' ' | sed 's/ *$//')
  WANT=$(printf '%s\n' $SELF_CASES | sort -u | tr '\n' ' ' | sed 's/ *$//')
  rm -f "$SELF_LOG"
  if [ "$RAN" != "$WANT" ]; then
    echo "SELFTEST FAIL: leg (e) evaluated [$RAN] but this selftest has"
    echo "               cases for [$WANT]. Add a case, or drop one."
    exit 1
  fi
  echo "selftest: leg (e) evaluated [$RAN], one case each"
  # stillclimbing needs NO sabotage hook: it is the real gate, stopped
  # before the cache plateaus. The bound and the cache-is-filling checks
  # both pass on it, so it reds the plateau assertion alone -- which had
  # no case until 2026-09-20 and could have been deleted unnoticed.
  for sab in unbounded nocache stillclimbing; do
    case $sab in
      unbounded)    want="not bounded by it"; env="MEM_N=200" ;;
      nocache)      want="not filling";       env="MEM_N=200" ;;
      stillclimbing) want="has not plateaued"; env="MEM_CAP=24 MEM_N=140" ;;
    esac
    case $sab in
      stillclimbing) out=$(env FARM_N=200 $env "$0" 2>&1) && rc=0 || rc=$? ;;
      *) out=$(env FARM_N=200 $env MEM_SABOTAGE=$sab "$0" 2>&1) && rc=0 || rc=$? ;;
    esac
    if [ "$rc" = "0" ]; then
      echo "SELFTEST FAIL: the $sab sabotage PASSED the gate"; fails=$((fails+1))
    elif ! printf '%s' "$out" | grep -q "$want"; then
      echo "SELFTEST FAIL: the $sab sabotage failed, but not on its own"
      echo "               assertion (wanted \"$want\"):"
      printf '%s\n' "$out" | grep -E "^SOAK FAIL" || true
      fails=$((fails+1))
    else
      echo "selftest: the $sab sabotage reds \"$want\""
    fi
  done
  [ "$fails" = "0" ] || { echo "SELFTEST FAIL: $fails of 4"; exit 1; }
  echo "SELFTEST PASS: the control passes and all three faults red their own assertion"
  exit 0
fi
SWE_HOME=${SWE_HOME:-/shares/swisseph}
FARM_N=${FARM_N:-100000}
STARTUP_MAX=${STARTUP_MAX:-2}
FD_TOL=${FD_TOL:-20}
BARRAGE=${BARRAGE:-200}
PORT=$((29400 + $$ % 400))   # below the ephemeral range: ephsrv-robust.sh says why
SCRATCH=$(mktemp -d /tmp/ephsrv-soak.XXXXXX)
FARM="$SCRATCH/farm"
EPHD_PID=
EPHD_LOG="$SCRATCH/ephd.log"

cleanup() {
  # The traced run's EPHD_PID is strace's, and killing strace leaves the
  # server it traced alive (two of them were found running an hour after
  # their gates, 2026-09-16), so the children go first.
  if [ -n "$EPHD_PID" ]; then
    pkill -P "$EPHD_PID" 2>/dev/null || true
    kill "$EPHD_PID" 2>/dev/null || true
  fi
  [ -z "${KEEP_FARM:-}" ] && rm -rf "$SCRATCH"
  return 0
}
trap cleanup EXIT

[ -x "$ROOT/astrolog-ephd" ] || make -s ephsrv >/dev/null
[ -x "$ROOT/eph_wsclient" ] || make -s ephsrv >/dev/null

# A real asteroid file to source. The repo's ephem/ carries se00005.se1
# (asteroid 5); anything on the machine's ephemeris path works.
SOURCE_SE1=$(ls "$ROOT"/ephem/se00005.se1 2>/dev/null || true)
if [ -z "$SOURCE_SE1" ]; then
  for d in ${SWE_HOME}/ephe "$ROOT/ephem"; do
    [ -f "$d/se00005.se1" ] && SOURCE_SE1="$d/se00005.se1" && break
  done
fi
[ -n "$SOURCE_SE1" ] || { echo "SOAK FAIL: no se00005.se1 to source the farm from"; exit 1; }

# The farm: ast<dir>/se<id:05d>.se1 for ids 0..FARM_N-1, dirs of 1000.
# Symlinks: the soak measures scanning behavior and fd hygiene, not SWE
# math, and a symlink is one stat per hit like a real file is. Id 5 gets
# the REAL file so some barrage requests genuinely open and retain data.
echo "building farm: $FARM_N asteroids under $FARM"
mkdir -p "$FARM"
python3 - "$FARM" "$FARM_N" "$SOURCE_SE1" << 'EOF'
import os, sys
farm, n, real = sys.argv[1], int(sys.argv[2]), sys.argv[3]
for ast in range(n):
    d = os.path.join(farm, "ast%d" % (ast // 1000))
    os.makedirs(d, exist_ok=True)
    f = os.path.join(d, "se%05d.se1" % ast)
    os.symlink(real, f)
EOF
# The main files, so the farm is an ephemeris directory in its own right.
# The server's --ephe is the whole search path since 2026-09-16; before
# that a farm without them borrowed ./ephem's through the fallback, which
# is exactly the silent substitution that was removed.
for f in sepl_18.se1 semo_18.se1 seas_18.se1; do
  ln -s "$(dirname "$SOURCE_SE1")/$f" "$FARM/$f"
done

# a. + b. Startup under strace, timed, with the farm present. The strace
# a. Startup TIMED with no tracer attached: strace's per-syscall cost
# dwarfs anything the server does (measured: 4.1s traced vs 0.2s untraced
# on a 2000-file farm), so the O(1) claim is timed clean and the scan
# count comes from its own traced run below.
STARTED=$(date +%s.%N)
"$ROOT/astrolog-ephd" --port "$PORT" --ephe "$FARM" --threads 1 \
  > "$EPHD_LOG" 2>&1 &
EPHD_PID=$!
for i in $(seq 1 $((STARTUP_MAX * 20))); do
  grep -q "evt=listen port=" "$EPHD_LOG" 2>/dev/null && break
  sleep 0.1
done
ENDED=$(date +%s.%N)
STARTUP_S=$(echo "$ENDED $STARTED" | awk '{printf "%.2f", $1 - $2}')
grep -q "evt=listen port=" "$EPHD_LOG" || { echo "SOAK FAIL: server did not start in ${STARTUP_MAX}s"; exit 1; }
awk -v s="$STARTUP_MAX" -v t="$STARTUP_S" 'BEGIN { exit !(t <= s) }' \
  || { echo "SOAK FAIL: startup took ${STARTUP_S}s > ${STARTUP_MAX}s with $FARM_N files"; exit 1; }
echo "startup: ${STARTUP_S}s with $FARM_N files (bound ${STARTUP_MAX}s)"

# b. Zero directory scans, a separate traced run. opendir is libc, not a
# syscall; the scan that would betray a tree walk is getdents64. The
# traced run is not the timed one, and the assertion covers the WHOLE log
# -- a getdents64 on the farm at any point, startup or later, is a
# violation, because nothing may ever walk the tree.
kill "$EPHD_PID" 2>/dev/null; wait "$EPHD_PID" 2>/dev/null || true
EPHD_PID=
# -y prints each descriptor's path: without it strace names getdents64's
# descriptor by number only, and the grep below could never match -- a
# server that walked the whole farm passed (EPHEMERIS_REVIEW.md T1).
strace -y -o "$SCRATCH/strace.log" -e trace=getdents64,openat \
  "$ROOT/astrolog-ephd" --port "$PORT" --ephe "$FARM" --threads 1 \
  > "$EPHD_LOG" 2>&1 &
EPHD_PID=$!
for i in $(seq 1 $((STARTUP_MAX * 20))); do
  grep -q "evt=listen port=" "$EPHD_LOG" 2>/dev/null && break
  sleep 0.1
done
grep -q "evt=listen port=" "$EPHD_LOG" || { echo "SOAK FAIL: traced run did not start"; exit 1; }
if grep -E "getdents64.*$FARM" "$SCRATCH/strace.log" > /dev/null; then
  echo "SOAK FAIL: the tree was scanned:"
  grep -E "getdents64.*$FARM" "$SCRATCH/strace.log" | head -5
  exit 1
fi
echo "startup scans: zero getdents64 on the farm"

# The SERVER's descriptors: EPHD_PID is strace's, and counting strace's
# own descriptors let a server leaking one per request pass (review T1).
SRV_PID=$(pgrep -P "$EPHD_PID" -x astrolog-ephd | head -1)
[ -n "$SRV_PID" ] || { echo "SOAK FAIL: no astrolog-ephd under strace $EPHD_PID"; exit 1; }
fdbusy() { ls "/proc/$SRV_PID/fd" 2>/dev/null | wc -l; }

# A handful of ids that REALLY resolve (the real se00005.se1 at ast0/se00005)
# plus misses across the farm's tail, so the barrage mixes opens, header
# rejections and clean per-object errors.
BARRAGE_IDS="20000005,20000006,20000007,20000008,20000009,20000010,20000011,20000012,20000013,20000014"
# One round of each request kind first: the first answer opens the files a
# request needs, which is the steady state, not a leak. Counted from
# before any request, the check measured those opens.
"$ROOT/eph_wsclient" --port "$PORT" --objs "$BARRAGE_IDS" --jd 2451545.0 \
  --step 600 --count 1 --quiet > /dev/null 2>&1 || true
"$ROOT/eph_wsclient" --port "$PORT" --objs 20100000 --jd 2451545.0 \
  --step 600 --count 1 --quiet > /dev/null 2>&1 || true
FD0=$(fdbusy)
i=0
while [ "$i" -lt "$BARRAGE" ]; do
  # a miss from the high tail every third request
  if [ $((i % 3)) -eq 0 ]; then
    "$ROOT/eph_wsclient" --port "$PORT" --objs "$((20100000 + i))" --jd 2451545.0 \
      --step 600 --count 1 --quiet > /dev/null 2>&1 || true
  else
    "$ROOT/eph_wsclient" --port "$PORT" --objs "$BARRAGE_IDS" --jd 2451545.0 \
      --step 600 --count 1 --quiet > /dev/null 2>&1 || true
  fi
  i=$((i + 1))
done
FD1=$(fdbusy)
GROWTH=$((FD1 - FD0))
echo "fds: $FD0 before, $FD1 after $BARRAGE requests (growth $GROWTH, tol $FD_TOL)"
awk -v g="$GROWTH" -v t="$FD_TOL" 'BEGIN { exit !(g <= t && g >= -t) }' \
  || { echo "SOAK FAIL: fd count moved by $GROWTH over the barrage"; exit 1; }

# d. A missing asteroid: per-object failure, request succeeds.
"$ROOT/eph_wsclient" --port "$PORT" --objs "$((20000000 + FARM_N + 12345))" --jd 2451545.0 \
  --step 600 --count 1 --out "$SCRATCH/miss.txt" --quiet
# The client's line: idx label errCode rowsOk flags columns. A missing file
# is A.17 code 4 (data unavailable) with no rows computed.
MISSERR=$(awk '{print $3}' "$SCRATCH/miss.txt")
MISSROWS=$(awk '{print $4}' "$SCRATCH/miss.txt")
[ "$MISSERR" = "4" ] && [ "$MISSROWS" = "0" ] \
  || { echo "SOAK FAIL: missing asteroid returned error $MISSERR with $MISSROWS rows, wanted error 4 and none"; exit 1; }
echo "missing asteroid: clean per-object failure (error 4, no rows), connection lived"

# e. Memory is bounded by --cache-mb, and the cache is really filling.
#
# This leg exists because the Prometheia project's load run watched our RSS
# climb 29.4 -> 89.4 MB over 30 seconds and could not say whether that was a
# cache filling or unbounded growth -- it never plateaued inside the run. It
# was the cache (measured: growth tracks --cache-mb exactly, 8MB cap grows
# 8MB and flattens by 100 windows, 64MB cap grows 64MB and flattens by 350).
# But nothing here could have ANSWERED that, because the fd bound above was
# the only resource this gate watched. A leaking result cache would have
# passed every gate in this project.
#
# TWO assertions, and the pairing is the point. The bound alone is green on
# a server whose cache stores nothing at all -- the same way an "these two
# agree" row is green when neither answer arrived. So the growth is required
# to be REAL as well as bounded.
#
# Its own daemon, and a COLD one, which is load-bearing in a way that is
# easy to optimize away later: this leg measures GROWTH, so a daemon whose
# cache has already reached its plateau grows by nothing and passes ANY
# bound. Reusing a warm daemon here -- across the legs above, across the
# selftest's cases, or by pointing this at something long-running -- turns
# the assertion into a confident green that measures nothing. The
# Prometheia project shipped exactly that caveat in their own load tool
# and found it by reusing one daemon across their selftest's cases.
#
# It also needs an unthrottled budget (one 3000-cell window a request
# would otherwise drain the default 100000-cell bucket in 33 requests and
# then trickle) and a small cap, so it fills in seconds rather than the 56
# minutes the shipped defaults would take.
MEM_CAP=${MEM_CAP:-8}
MEM_N=${MEM_N:-300}
MEM_PORT=$((PORT + 1))
# MEM_SABOTAGE is the fault injection --selftest drives, and it is here
# rather than in a copy of this leg so that what is falsified is THIS code
# (see --selftest below). "unbounded" gives the daemon a cache far larger
# than the bound asserted, which is what a cache that stopped evicting
# looks like from outside; "nocache" disables it, which is what a bound
# that proves nothing looks like.
MEM_CACHE=$MEM_CAP
case "${MEM_SABOTAGE:-}" in
  unbounded) MEM_CACHE=$((MEM_CAP * 16)) ;;
  nocache)   MEM_CACHE=0 ;;
esac
"$ROOT/astrolog-ephd" --bind 127.0.0.1 --port "$MEM_PORT" --threads 1 \
  --cache-mb "$MEM_CACHE" --cells-per-sec 0 --ephe "$ROOT/ephem;$ROOT" \
  > "$SCRATCH/ephd-mem.log" 2>&1 &
MEM_PID=$!
sleep 3
kill -0 "$MEM_PID" 2>/dev/null \
  || { echo "SOAK FAIL: the memory leg's daemon did not start"; exit 1; }
memrss() { awk '/VmRSS/{print $2}' "/proc/$MEM_PID/status" 2>/dev/null; }
MEM0=$(memrss)
i=0
while [ "$i" -lt "$MEM_N" ]; do
  # DISTINCT windows: every one a miss, so every one is stored.
  JD=$(awk -v i="$i" 'BEGIN{ printf "%.6f", 2451545.0 + i * 0.37 }')
  "$ROOT/eph_wsclient" --port "$MEM_PORT" --quiet --jd "$JD" \
    --step-ns 3600000000000 --count 500 \
    --objs 10,301,199,299,499,5,6,7,8,9 --out /dev/null > /dev/null 2>&1 || true
  i=$((i + 1))
  [ "$i" = "$((MEM_N / 2))" ] && MEMHALF=$(memrss)
done
MEM1=$(memrss)
kill "$MEM_PID" 2>/dev/null || true
GREW=$(( (MEM1 - MEM0) / 1024 ))
SETTLE=$(( (MEM1 - MEMHALF) / 1024 ))
echo "memory: ${GREW}MB growth over $MEM_N distinct windows, cap ${MEM_CAP}MB;" \
     "${SETTLE}MB more in the second half"
# Every leg-(e) assertion goes through memassert, so the list of
# assertions the selftest checks itself against is WHAT RAN, not what a
# pattern matched in this file. The first version grepped these messages
# out of the source, which the Prometheia project pointed out trades a
# list that can rot for a regex that can rot -- and the regex rots GREEN:
# it stops matching, the derived list shrinks to nothing, and every case
# is trivially covered. Recording at evaluation has no such direction.
#
# $1 token, $2 the test's exit status, $3 the message.
memassert() {
  # Two set -e traps live here and both were hit on the commit that
  # introduced this helper. A BARE "[ ... ]" whose test fails exits the
  # script instantly under set -e, before any message -- which is why the
  # callers write "memok=0; [ ... ] || memok=$?", a form set -e exempts.
  # And "[ -n "$X" ] && printf" as a statement fails when X is unset,
  # which is every run but the control. The selftest caught both as three
  # sabotages failing "not on their own assertion", which is exactly the
  # clause it exists for, on the commit that broke it.
  if [ -n "${MEM_ASSERT_LOG:-}" ]; then
    printf '%s\n' "$1" >> "$MEM_ASSERT_LOG"
  fi
  [ "$2" = "0" ] && return 0
  echo "SOAK FAIL: $3"
  exit 1
}
# Bounded: the cap plus slack for the farm's own working set.
memok=0; [ "$GREW" -le "$((MEM_CAP + 12))" ] || memok=$?
memassert bounded $memok \
  "RSS grew ${GREW}MB against a ${MEM_CAP}MB cache cap -- the result cache is not bounded by it"
# Real: a cache that stored nothing would also be "bounded".
memok=0; [ "$GREW" -ge "$((MEM_CAP / 2))" ] || memok=$?
memassert filling $memok \
  "RSS grew only ${GREW}MB against a ${MEM_CAP}MB cap -- the cache is not filling, so the bound above proves nothing"
# Plateaued: the second half must add far less than the first.
memok=0; [ "$SETTLE" -le 2 ] || memok=$?
memassert plateaued $memok \
  "RSS rose ${SETTLE}MB in the second half of the run -- it has not plateaued, so this cannot tell a filling cache from a leak"

echo "SOAK PASS: farm of $FARM_N files, startup ${STARTUP_S}s, zero scans, fds stable, memory bounded by the cache cap"
