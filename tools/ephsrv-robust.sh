#!/bin/bash
# tools/ephsrv-robust.sh -- the ephemeris server against hostile and heavy
# clients: the server findings of EPHEMERIS_REVIEW.md (2026-09-16), one
# assertion each, every one seen failing on the server before its fix.
#
#   S1  a REQUEST whose jdStart is NaN or infinite is refused ERROR 1 and
#       the server stays up (it used to crash every loop: the fork indexes
#       a table with (int)floor(NaN) for the mean node and apogee)
#   S2  a big window to a client that reads nothing for three seconds
#       arrives with every row exactly once (chunks that met backpressure
#       used to be sent twice, and a row-counting client finished early
#       with zeros)
#   S3  20000 connect/close cycles do not grow the server's resident
#       memory by more than 4 MiB (a second Conn constructed over the live
#       one leaked ~575 bytes each: 11 MiB over the same run)
#   S4  a client that pipelines ten big requests and reads nothing is
#       refused past the queued-answer cap, and the server still answers
#   S5  --threads far above the context pool answers every connection
#       (loops past the pool had no context and answered ERROR 4)
#   S6  a second server on a port another one listens on exits nonzero,
#       rather than sharing the port and half its connections
#   S10 a second HELLO on one connection is answered
#
# Knobs (env): SWE_HOME, EPH, PORT (default 29000 + pid % 400, below the
# ephemeral range; see below), STEP_TIMEOUT (seconds per client run, 60).
#
# Exit 0 with "ROBUST PASS"; nonzero naming the failed assertion.

set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
SWE_HOME=${SWE_HOME:-/shares/swisseph}
EPH=${EPH:-$ROOT/ephem}
# Below the kernel's ephemeral range (32768 up, by default). S3 makes
# 20000 connections, and for a minute after, their TIME_WAIT sockets hold
# that many local ports scattered across the range -- a port chosen inside
# it was then refused to the next server that asked (measured: two runs of
# three failed S5 with "cannot listen ... in use?" on a port nothing was
# listening on).
PORT=${PORT:-$((29000 + $$ % 400))}
SCRATCH=$(mktemp -d /tmp/ephsrv-robust.XXXXXX)
PIDS=()
FAIL=0

cleanup() {
  for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null || true; done
  rm -rf "$SCRATCH"
}
trap cleanup EXIT

[ -x "$ROOT/astrolog-ephd" ] && [ -x "$ROOT/eph_wsclient" ] || make -s ephsrv >/dev/null
# Every client run is bounded, and every step says it started, so a
# stall names its step instead of hanging the gate (its first draft did).
CLI_BIN="$ROOT/eph_wsclient"
CLI() { timeout "${STEP_TIMEOUT:-60}" "$CLI_BIN" "$@"; }
CLI="CLI"
step() { echo "== $*"; }
fail() { echo "ROBUST FAIL: $*"; FAIL=1; }

# start_server <port> <log> [args...]; sets SRV_PID
start_server() {
  local port=$1 log=$2; shift 2
  "$ROOT/astrolog-ephd" --port "$port" --ephe "$EPH" "$@" > "$log" 2>&1 &
  SRV_PID=$!
  PIDS+=("$SRV_PID")
  for _ in $(seq 1 100); do
    grep -q "listening on port" "$log" 2>/dev/null && return 0
    kill -0 "$SRV_PID" 2>/dev/null || return 1
    sleep 0.05
  done
  return 1
}
alive() { kill -0 "$1" 2>/dev/null; }
rss_kib() { awk '/^VmRSS:/ {print $2}' "/proc/$1/status"; }

start_server "$PORT" "$SCRATCH/a.log" --threads 1 || { echo "ROBUST FAIL: server did not start"; exit 1; }
A=$SRV_PID

# ---- S1: non-finite instants --------------------------------------------
step "S1"
for jd in nan -nan inf -inf; do
  for tt in "" "--tt"; do
    if $CLI --port "$PORT" --objs 10,12 --jd "$jd" --count 1 --quiet $tt \
        2> "$SCRATCH/s1.err" >/dev/null; then
      fail "S1: a REQUEST with jdStart $jd $tt was answered"
    elif ! grep -q "server ERROR 1" "$SCRATCH/s1.err"; then
      fail "S1: jdStart $jd $tt: expected ERROR 1, got: $(head -1 "$SCRATCH/s1.err")"
    fi
  done
done
alive "$A" || { echo "ROBUST FAIL: S1: the server died on a non-finite jdStart"; exit 1; }
$CLI --port "$PORT" --objs 10,12 --count 1 --quiet \
  || fail "S1: an ordinary request after the non-finite ones was not answered"

# ---- S2: every row exactly once under backpressure ----------------------
step "S2"
# 64 objects, all the Sun: the cheapest body, so the window is big on the
# wire (64 x 10000 rows = 29 MiB, past the socket buffers and uWS's 4 MiB
# cap) without being slow to compute.
BODIES=$(printf '0%.0s,' $(seq 1 63))0
if ! $CLI --port "$PORT" --objs "$BODIES" --count 10000 --step 60 \
    --sleep-ms 3000 --quiet --expect-rows 10000 2> "$SCRATCH/s2.err"; then
  fail "S2: a 64x10000 window to a sleeping reader: $(head -1 "$SCRATCH/s2.err")"
fi

# ---- S10: a second HELLO is answered -------------------------------------
step "S10"
$CLI --port "$PORT" --hello 3 --objs 0 --count 1 --quiet \
  || fail "S10: three HELLOs on one connection did not each get a WELCOME"

# ---- S4: the queued-answer cap -------------------------------------------
step "S4"
# Ten copies of S2's 29 MiB window, which the cache already holds: each
# answer is bigger than the socket buffers and uWS's 4 MiB, so an unread
# one stays queued, and none of them costs a computation. (Distinct 6 MiB
# windows were the first form, and loopback's buffers swallowed them whole:
# all ten answered, nothing ever queued.)
if ! $CLI --port "$PORT" --objs "$BODIES" --count 10000 --step 60 --burst 10 --burst-step 0 \
    --sleep-ms 2000 --quiet > "$SCRATCH/s4.out" 2> "$SCRATCH/s4.err"; then
  fail "S4: a burst of ten big requests: $(head -1 "$SCRATCH/s4.err")"
else
  refused=$(sed -nE 's/.* ([0-9]+) refused ERROR 2/\1/p' "$SCRATCH/s4.out")
  answered=$(sed -nE 's/burst: ([0-9]+) answered.*/\1/p' "$SCRATCH/s4.out")
  [ "${refused:-0}" -ge 1 ] || fail "S4: no request of the burst was refused ($(cat "$SCRATCH/s4.out"))"
  [ "${answered:-0}" -ge 1 ] || fail "S4: no request of the burst was answered ($(cat "$SCRATCH/s4.out"))"
fi
alive "$A" || fail "S4: the server died under the burst"
$CLI --port "$PORT" --objs 0,1 --count 1 --quiet || fail "S4: not answering after the burst"

# ---- S6: a second server on the same port --------------------------------
step "S6"
"$ROOT/astrolog-ephd" --port "$PORT" --ephe "$EPH" > "$SCRATCH/b.log" 2>&1 &
B=$!
PIDS+=("$B")
sleep 1.5
if alive "$B"; then
  fail "S6: a second server on port $PORT is still running ($(tail -1 "$SCRATCH/b.log"))"
elif ! grep -q "cannot listen on port" "$SCRATCH/b.log"; then
  fail "S6: the second server exited without saying why: $(tail -1 "$SCRATCH/b.log")"
fi
kill "$A" 2>/dev/null || true
wait "$A" 2>/dev/null || true

# ---- S5: loops beyond the context pool -----------------------------------
step "S5"
PORT2=$((PORT + 1))
NT=$(( $(nproc) * 4 ))
start_server "$PORT2" "$SCRATCH/c.log" --threads "$NT" || { fail "S5: server with --threads $NT did not start: $(tail -2 "$SCRATCH/c.log" | tr '\n' ' ')"; }
C=$SRV_PID
bad=0
for i in $(seq 1 40); do
  $CLI --port "$PORT2" --objs 0,1 --jd "$((2451545 + i))" --count 1 --quiet 2>>"$SCRATCH/s5.err" || bad=$((bad + 1))
done
[ "$bad" -eq 0 ] || fail "S5: $bad of 40 connections to a $NT-loop server failed: $(head -1 "$SCRATCH/s5.err")"

# ---- S3: no per-connection leak (last: 20000 connections leave the
step "S3"
# ephemeral range full of TIME_WAIT, which the servers above do not need)
$CLI --port "$PORT2" --cycles 2000 --objs 0 --quiet || fail "S3: warm-up cycles failed"
R0=$(rss_kib "$C")
$CLI --port "$PORT2" --cycles 20000 --objs 0 --quiet || fail "S3: 20000 cycles failed"
R1=$(rss_kib "$C")
GROW=$((R1 - R0))
[ "$GROW" -le 4096 ] || fail "S3: resident memory grew $GROW KiB over 20000 connections"

kill "$C" 2>/dev/null || true

[ "$FAIL" -eq 0 ] || exit 1
echo "ROBUST PASS"
