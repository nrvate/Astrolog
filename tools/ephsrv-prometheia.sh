#!/bin/sh
# tools/ephsrv-prometheia.sh -- Astrolog's real `server` source against
# Ephemeris Prometheia's daemon: the cross-ENGINE gate.
#
#   tools/ephsrv-prometheia.sh [prometheiad]
#   tools/ephsrv-prometheia.sh --selftest    # prove this gate can fail
#
# Every other server gate compares astrolog-ephd with the Swiss library it
# links, so the answers are bit-identical by construction and a gate can
# only ask "did anything change". This one asks a chart's worth of questions
# of a DIFFERENT engine (DE440 and the EPM1 small-body catalog against
# Swiss's own files) and holds the answers to bounds: planets and points
# 0.3", small bodies 1", longitude rates 3e-4 deg/d. Angular SEPARATIONS,
# never longitude differences. The bounds are the worst measured over the
# eleven scenarios with about a third on top (CLIENT_SERVER_REVIEW.md C1a);
# the measured worst was 0.22" (Neptune, 1900), 0.85" (Orcus), 1.8e-4 deg/d.
#
# It is the group "Ephemeris server, live parity" with ASTROLOG_EPHSRV_URL
# set, which turns the bit-identity assertions into these bounds and skips the
# legs that only make sense between two copies of one engine. The daemon is
# started here on a port of its own and stopped by the PID this script
# started, never by name: other servers belonging to other people run on this
# machine. Run BY HAND, like every other eph gate -- it needs the other
# project's build, and skips with a printed reason without it.
#
# --selftest scales every bound by 0.01 (ASTROLOG_XENGINE_SCALE) and requires
# the run to FAIL, after requiring the unscaled run to pass: a gate whose
# bound cannot be exceeded says nothing by passing.

set -u

SELFTEST=0
[ "${1:-}" = "--selftest" ] && { SELFTEST=1; shift; }
P=/shares/ephemeris-prometheia
PROMD="${1:-$P/build/prometheiad}"
EPHE="$P/ephe/linux_p1550p2650.440"
CAT="$P/sbdb-full-20260916.epm"
PERT="$P/ephe/sb441-n16-de440span.bsp"
BIN="./astrolog-qt-test"
WORK="${TMPDIR:-/nvm/work}/ephsrv-prometheia.$$"

for f in "$PROMD" "$EPHE" "$CAT" "$PERT" "$BIN"; do
  if [ ! -e "$f" ]; then
    echo "ephsrv-prometheia: $f is not there; skipping (needs the Prometheia"
    echo "  build and data, and our own \"make qt-test\")"
    exit 0
  fi
done

mkdir -p "$WORK" || exit 1
PID=""
cleanup() {
  [ -n "$PID" ] && kill "$PID" 2>/dev/null
  [ -n "$PID" ] && wait "$PID" 2>/dev/null
  rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

"$PROMD" --ephemeris "$EPHE" --catalog "$CAT" --perturbers "$PERT" \
  --bind 127.0.0.1 --port 0 --threads 1 > "$WORK/prom.log" 2>&1 &
PID=$!
PORT=""
i=0
while [ $i -lt 300 ]; do
  PORT=$(sed -n 's/.*listening on port \([0-9][0-9]*\).*/\1/p' "$WORK/prom.log" | head -1)
  [ -n "$PORT" ] && break
  kill -0 "$PID" 2>/dev/null || { echo "ephsrv-prometheia FAIL: prometheiad exited:"; cat "$WORK/prom.log"; exit 1; }
  sleep 0.1
  i=$((i + 1))
done
[ -n "$PORT" ] || { echo "ephsrv-prometheia FAIL: prometheiad never listened"; exit 1; }

run() {   # run <scale>: the group, against the daemon; prints its own result
  env -u DISPLAY QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= \
    ASTROLOG_EPHSRV_URL="localhost:$PORT" ASTROLOG_XENGINE_SCALE="$1" \
    ASTROLOG_QT_TESTS=ephem-server-live ./run-qt-tests.sh > "$WORK/run.$1.log" 2>&1
  return $?
}

run 1; RC=$?
grep -E ' main |FAIL|PASS:' "$WORK/run.1.log" | cut -c1-200
if [ $RC -ne 0 ]; then
  echo "EPHSRV-PROMETHEIA FAIL: the cross-engine legs did not pass"
  exit 1
fi
# The houses leg (2026-09-30): the daemon's house points (object kind 6) graded
# by the same gate as our own server's, against tools/houses_ref.py at each
# row's own ARMC and obliquity. A different engine with its own sidereal time
# and its own house code, so this is the check that a fault the two share
# cannot hide from -- it found that Whole Sign ANGLES had been given rate 0.
if [ -x eph_wsclient ]; then
  python3 tools/ephsrv_houses.py "$PORT" > "$WORK/houses.log" 2>&1
  RCH=$?
  tail -3 "$WORK/houses.log"
  if [ $RCH -ne 0 ]; then
    echo "EPHSRV-PROMETHEIA FAIL: the houses leg did not pass"
    exit 1
  fi
fi
if [ $SELFTEST -eq 1 ]; then
  run 0.01; RC=$?
  if [ $RC -eq 0 ]; then
    echo "EPHSRV-PROMETHEIA SELFTEST FAIL: bounds scaled by 0.01 still passed"
    exit 1
  fi
  N=$(grep -c 'FAIL  .*agree' "$WORK/run.0.01.log")
  [ "$N" -ge 11 ] || { echo "EPHSRV-PROMETHEIA SELFTEST FAIL: only $N bound assertions failed at 0.01"; exit 1; }
  echo "EPHSRV-PROMETHEIA SELFTEST PASS: unscaled passes, 0.01 fails $N bound assertions"
  exit 0
fi
echo "EPHSRV-PROMETHEIA PASS"
