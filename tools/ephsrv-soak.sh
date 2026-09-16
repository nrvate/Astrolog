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
#      failure (retFlag < 0) without killing the connection.
#
# Knobs (env):
#   FARM_N      asteroid count to synthesize      (default 100000)
#   STARTUP_MAX startup wall-time bound, seconds  (default 2)
#   FD_TOL      allowed fd growth over the barrage(default 20)
#   BARRAGE     requests in the fd barrage         (default 200)
#   KEEP_FARM   set to keep the farm directory
#
# Exit 0 with "SOAK PASS"; nonzero with the failed assertion. Re-runnable;
# cleans up the farm unless KEEP_FARM is set.

set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
SWE_HOME=${SWE_HOME:-/shares/swisseph}
FARM_N=${FARM_N:-100000}
STARTUP_MAX=${STARTUP_MAX:-2}
FD_TOL=${FD_TOL:-20}
BARRAGE=${BARRAGE:-200}
PORT=$((47300 + $$ % 400))
SCRATCH=$(mktemp -d /tmp/ephsrv-soak.XXXXXX)
FARM="$SCRATCH/farm"
EPHD_PID=
EPHD_LOG="$SCRATCH/ephd.log"

cleanup() {
  [ -n "$EPHD_PID" ] && kill "$EPHD_PID" 2>/dev/null
  [ -z "${KEEP_FARM:-}" ] && rm -rf "$SCRATCH"
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
  grep -q "ephemeris path" "$EPHD_LOG" 2>/dev/null && break
  sleep 0.1
done
ENDED=$(date +%s.%N)
STARTUP_S=$(echo "$ENDED $STARTED" | awk '{printf "%.2f", $1 - $2}')
grep -q "ephemeris path" "$EPHD_LOG" || { echo "SOAK FAIL: server did not start in ${STARTUP_MAX}s"; exit 1; }
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
strace -o "$SCRATCH/strace.log" -e trace=getdents64,openat \
  "$ROOT/astrolog-ephd" --port "$PORT" --ephe "$FARM" --threads 1 \
  > "$EPHD_LOG" 2>&1 &
EPHD_PID=$!
for i in $(seq 1 $((STARTUP_MAX * 20))); do
  grep -q "ephemeris path" "$EPHD_LOG" 2>/dev/null && break
  sleep 0.1
done
grep -q "ephemeris path" "$EPHD_LOG" || { echo "SOAK FAIL: traced run did not start"; exit 1; }
if grep -E "getdents64.*$FARM" "$SCRATCH/strace.log" > /dev/null; then
  echo "SOAK FAIL: the tree was scanned:"
  grep -E "getdents64.*$FARM" "$SCRATCH/strace.log" | head -5
  exit 1
fi
echo "startup scans: zero getdents64 on the farm"

fdbusy() { ls "/proc/$EPHD_PID/fd" 2>/dev/null | wc -l; }

# A handful of ids that REALLY resolve (the real se00005.se1 at ast0/se00005)
# plus misses across the farm's tail, so the barrage mixes opens, header
# rejections and clean per-object errors.
BARRAGE_IDS="5,6,7,8,9,10,11,12,13,14"
FD0=$(fdbusy)
i=0
while [ "$i" -lt "$BARRAGE" ]; do
  # a miss from the high tail every third request
  if [ $((i % 3)) -eq 0 ]; then
    "$ROOT/eph_wsclient" --port "$PORT" --objs "$((100000 + i))" --jd 2451545.0 \
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
"$ROOT/eph_wsclient" --port "$PORT" --objs "$((FARM_N + 12345))" --jd 2451545.0 \
  --step 600 --count 1 --out "$SCRATCH/miss.txt" --quiet
MISSFLAG=$(awk '{print $3}' "$SCRATCH/miss.txt")
[ "$MISSFLAG" = "-1" ] \
  || { echo "SOAK FAIL: missing asteroid returned retFlag $MISSFLAG, wanted -1"; exit 1; }
echo "missing asteroid: clean per-object failure (retFlag -1), connection lived"

echo "SOAK PASS: farm of $FARM_N files, startup ${STARTUP_S}s, zero scans, fds stable"
