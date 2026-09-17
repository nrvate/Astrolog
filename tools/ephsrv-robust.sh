#!/bin/bash
# tools/ephsrv-robust.sh -- the ephemeris server against hostile and heavy
# clients: the server findings of EPHEMERIS_REVIEW.md (2026-09-16), one
# assertion each, every one seen failing on the server before its fix.
#
#   V4  protocol version 4's refusals: an older client's HELLO (version 3)
#       gets ERROR 8 in that client's own layout and version, and the
#       connection closes; a client needing a newer protocol gets ERROR 8
#       in 4; a newer client (protoMax 5) is welcomed in 4; a message
#       before HELLO is ERROR 1 and closes; every malformed and unsupported
#       conformance fixture is refused ERROR 1 or 11 as its MANIFEST says,
#       and the connection still answers after each
#   C1  CANCEL: a big answer cancelled while it streams loses its unsent
#       chunks and is answered ERROR 10, nothing of it follows, the
#       connection answers the next request, and the same window asked
#       again comes back WHOLE -- a partial answer never becomes a cache
#       entry (3.9)
#   P1  priority: an interactive request queued behind two prefetch ones
#       that have not begun streaming is answered before them (3.9)
#   L1  LOOKUP: a name, an alias, a number and a prefix resolve to objects
#       a REQUEST can ask for, and a query that matches nothing answers
#       nothing
#   S1  a REQUEST whose start instant is NaN or infinite is refused ERROR 1
#       and the server stays up (it used to crash every loop: the fork
#       indexes a table with (int)floor(NaN) for the mean node and apogee)
#   S2  a big window to a client that reads nothing for three seconds
#       arrives with every row exactly once (chunks that met backpressure
#       used to be sent twice, and a row-counting client finished early
#       with zeros)
#   S3  20000 connect/close cycles do not grow the server's resident
#       memory by more than 4 MiB (a second Conn constructed over the live
#       one leaked ~575 bytes each: 11 MiB over the same run)
#   S4  a client that pipelines ten big requests and reads nothing is
#       refused ERROR 9 (busy) past the queued-answer cap, and the server
#       still answers; a request past the work bound WELCOME advertises
#       (objects x rows) is refused ERROR 2, and the same request one row
#       under it is answered
#   S9  a window crossing an ephemeris file's end answers the rows that
#       computed as real numbers and the rows past it as NaN, the object
#       marked partial with its rows counted -- version 1 failed the object
#       for the whole window, so every frame before the edge was lost too
#   S5  --threads far above the context pool answers every connection
#       (loops past the pool had no context and answered ERROR 4)
#   S6  a second server on a port another one listens on exits nonzero,
#       rather than sharing the port and half its connections
#   S10 a second HELLO on one connection is answered
#
# F8 (a PREC_ORIG sidereal mode's precession models reaching the next
# request on the loop) went with protocol 3: version 4's zodiac is a token
# and a plane, and SE_SIDBIT_PREC_ORIG has no spelling in it, so no request
# can set those models any more.
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
  # --cells-per-sec 0: these legs burst from one address on purpose, and the
  # compute budget is tools/ephsrv-limits.sh's to test, not theirs.
  "$ROOT/astrolog-ephd" --port "$port" --ephe "$EPH" --cells-per-sec 0 "$@" > "$log" 2>&1 &
  SRV_PID=$!
  PIDS+=("$SRV_PID")
  for _ in $(seq 1 100); do
    grep -q "evt=listen port=" "$log" 2>/dev/null && return 0
    kill -0 "$SRV_PID" 2>/dev/null || return 1
    sleep 0.05
  done
  return 1
}
alive() { kill -0 "$1" 2>/dev/null; }
rss_kib() { awk '/^VmRSS:/ {print $2}' "/proc/$1/status"; }

# The heavy legs below (S2, S4's burst) ask 64 x 10000 = 640000 cells, past
# the default work bound of 100000 -- this server is started with a bound
# that carries them, and the default bound is what the S4b leg below
# asserts.
start_server "$PORT" "$SCRATCH/a.log" --threads 1 --max-cells 700000 \
  || { echo "ROBUST FAIL: server did not start"; exit 1; }
A=$SRV_PID

# ---- S1: non-finite instants --------------------------------------------
step "S1"
for jd in nan -nan inf -inf; do
  for ut in "" "--ut"; do
    if $CLI --port "$PORT" --objs 301 --points 301:0:0,301:3:0 --jd "$jd" --count 1 --quiet $ut \
        2> "$SCRATCH/s1.err" >/dev/null; then
      fail "S1: a REQUEST starting at $jd $ut was answered"
    elif ! grep -q "server ERROR 1 " "$SCRATCH/s1.err"; then
      fail "S1: start $jd $ut: expected ERROR 1, got: $(head -1 "$SCRATCH/s1.err")"
    fi
  done
done
alive "$A" || { echo "ROBUST FAIL: S1: the server died on a non-finite instant"; exit 1; }
$CLI --port "$PORT" --points 301:0:0,301:3:0 --count 1 --quiet \
  || fail "S1: an ordinary request after the non-finite ones was not answered"

# ---- V4: version 4's refusals ---------------------------------------------
step "V4"
if $CLI --port "$PORT" --proto 3 --objs 10 --count 1 --quiet 2> "$SCRATCH/v4.err"; then
  fail "V4: a version-3 client was served"
elif ! grep -q "server ERROR 8 (HELLO, v3): .*update" "$SCRATCH/v4.err"; then
  fail "V4: a version-3 client got no ERROR 8 in its own layout: $(head -1 "$SCRATCH/v4.err")"
fi
if $CLI --port "$PORT" --proto 6 --proto-min 5 --objs 10 --count 1 --quiet 2> "$SCRATCH/v4.err"; then
  fail "V4: a client needing protocol 5 was served"
elif ! grep -q "server ERROR 8 (HELLO, v4) closing: " "$SCRATCH/v4.err"; then
  fail "V4: a client needing protocol 5: $(head -1 "$SCRATCH/v4.err")"
fi
$CLI --port "$PORT" --proto 5 --objs 10 --count 1 > "$SCRATCH/v4.out" 2> "$SCRATCH/v4.err" &&
  grep -q "^WELCOME v4 " "$SCRATCH/v4.out" || fail "V4: a newer client was not welcomed in 4: $(cat "$SCRATCH/v4.err")"
if $CLI --port "$PORT" --no-hello --objs 10 --count 1 --quiet 2> "$SCRATCH/v4.err"; then
  fail "V4: a REQUEST before HELLO was answered"
elif ! grep -q "server ERROR 1 (request 1) closing .*HELLO first" "$SCRATCH/v4.err"; then
  fail "V4: a REQUEST before HELLO: $(head -1 "$SCRATCH/v4.err")"
fi
# The conformance fixtures a client could send, each on its own connection
# after HELLO: an "ok" one must not draw ERROR 1 or 11, the others must
# draw exactly the ERROR their MANIFEST line names.
nfix=0
while IFS=$'\t' read -r file dir type expect note; do
  case "$file" in ''|'#'*) continue ;; esac
  [ "$dir" = c2s ] || continue
  [ "$type" = 1 ] && continue   # a second HELLO is S10's
  out=$($CLI --port "$PORT" --send-hex "ephsrv/conformance/$file" --quiet 2>&1) ||
    { fail "V4: $file: the client failed: $out"; continue; }
  code=$(echo "$out" | sed -nE 's/.*server ERROR ([0-9]+) .*/\1/p' | head -1)
  case "$expect" in
    malformed) [ "$code" = 1 ] || fail "V4: $file ($note): expected ERROR 1, got: $out" ;;
    unsupported) [ "$code" = 11 ] || fail "V4: $file ($note): expected ERROR 11, got: $out" ;;
    # A well-formed message can still ask for something this server does not
    # serve (segments) or does not have (another server's pinned dataset):
    # ERROR 11 and ERROR 5 are answers, ERROR 1 would be a parse this end
    # got wrong.
    ok) [ "$code" != 1 ] || fail "V4: $file ($note): a valid message was refused as malformed: $out" ;;
  esac
  nfix=$((nfix + 1))
done < ephsrv/conformance/MANIFEST.tsv
[ "$nfix" -ge 30 ] || fail "V4: only $nfix client fixtures were sent"
alive "$A" || { echo "ROBUST FAIL: V4: the server died on a conformance fixture"; exit 1; }
$CLI --port "$PORT" --objs 10 --count 1 --quiet || fail "V4: not answering after the fixtures"

# ---- C1: CANCEL -------------------------------------------------------------
step "C1"
# A 64 x 10000 window (29 MiB) to a reader asleep 1.5 s: most of it is still
# unsent when the CANCEL lands 0.2 s after the REQUEST.
BODIES=$(printf '10%.0s,' $(seq 1 63))10
if ! $CLI --port "$PORT" --objs "$BODIES" --count 10000 --step 60 --jd 2451600.5 \
    --sleep-ms 1500 --cancel-after-ms 200 --quiet > "$SCRATCH/c1.out" 2> "$SCRATCH/c1.err"; then
  fail "C1: the cancelled request: $(head -1 "$SCRATCH/c1.err")"
else
  got=$(sed -nE 's/cancel: ([0-9]+) of 10000 rows before ERROR 10/\1/p' "$SCRATCH/c1.out")
  [ -n "$got" ] && [ "$got" -lt 10000 ] || fail "C1: no ERROR 10 before the whole answer: $(cat "$SCRATCH/c1.out")"
fi
grep -q " evt=cancel .* rows=10000" "$SCRATCH/a.log" || fail "C1: the server logged no cancel"
$CLI --port "$PORT" --objs 10 --count 1 --quiet || fail "C1: not answering after the cancel"
# The cancelled window, asked again: whole rows, and no half answer cached.
$CLI --port "$PORT" --objs "$BODIES" --count 10000 --step 60 --jd 2451600.5 \
  --expect-rows 10000 --quiet 2> "$SCRATCH/c1b.err" ||
  fail "C1: the cancelled window asked again: $(head -1 "$SCRATCH/c1b.err")"

# ---- P1: interactive work before prefetch ----------------------------------
step "P1"
# Three requests at once, none read for two seconds: the first (prefetch)
# starts streaming and stalls, the second (prefetch) queues behind it, and
# the third (interactive) must be answered before that second one.
if ! $CLI --port "$PORT" --objs "$BODIES" --count 10000 --step 60 --jd 2451700.5 \
    --burst 3 --burst-step 1 --priority 1,1,0 --sleep-ms 2000 --quiet \
    > "$SCRATCH/p1.out" 2> "$SCRATCH/p1.err"; then
  fail "P1: the three-request burst: $(head -1 "$SCRATCH/p1.err")"
else
  grep -q "order 1,3,2" "$SCRATCH/p1.out" ||
    fail "P1: the interactive request was not answered before the queued prefetch one ($(cat "$SCRATCH/p1.out"))"
fi

# ---- L1: LOOKUP ------------------------------------------------------------
step "L1"
lookup() {   # lookup LABEL QUERY FLAGS GREP
  local label=$1 query=$2 flags=$3 want=$4
  $CLI --port "$PORT" --lookup "$query" --lookup-flags "$flags" --quiet > "$SCRATCH/l1.out" 2>&1 ||
    { fail "L1: $label: the lookup failed: $(head -1 "$SCRATCH/l1.out")"; return; }
  grep -q "$want" "$SCRATCH/l1.out" ||
    fail "L1: $label: no \"$want\" in $(tr '\n' ' ' < "$SCRATCH/l1.out")"
}
lookup "a canonical name" Ceres 0 'quality=0 kind=0 20000001 canonical="Ceres"'
lookup "an alias" "North Node" 0 'kind=1 o:301/0/0'
lookup "a number" 433 0 'kind=0 20000433'
lookup "a prefix" Ura 1 'quality=2 kind=0 7'
lookup "a hypothetical" Kronos 2 'kind=3 h:kronos'
lookup "a name of several kinds" Lilith 2 'LOOKUP n=2'
lookup "a name that matches nothing" Qwertyuiop 3 'LOOKUP n=0'
$CLI --port "$PORT" --objs 10 --count 1 --quiet || fail "L1: not answering after the lookups"

# ---- S2: every row exactly once under backpressure ----------------------
step "S2"
# 64 objects, all the Sun: the cheapest body, so the window is big on the
# wire (64 x 10000 rows = 29 MiB, past the socket buffers and uWS's 4 MiB
# cap) without being slow to compute.
if ! $CLI --port "$PORT" --objs "$BODIES" --count 10000 --step 60 \
    --sleep-ms 3000 --quiet --expect-rows 10000 2> "$SCRATCH/s2.err"; then
  fail "S2: a 64x10000 window to a sleeping reader: $(head -1 "$SCRATCH/s2.err")"
fi

# ---- S10: a second HELLO is answered -------------------------------------
step "S10"
$CLI --port "$PORT" --hello 3 --objs 10 --count 1 --quiet \
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
  refused=$(sed -nE 's/.* ([0-9]+) refused ERROR 9.*/\1/p' "$SCRATCH/s4.out")
  answered=$(sed -nE 's/burst: ([0-9]+) answered.*/\1/p' "$SCRATCH/s4.out")
  [ "${refused:-0}" -ge 1 ] || fail "S4: no request of the burst was refused ($(cat "$SCRATCH/s4.out"))"
  [ "${answered:-0}" -ge 1 ] || fail "S4: no request of the burst was answered ($(cat "$SCRATCH/s4.out"))"
fi
alive "$A" || fail "S4: the server died under the burst"
$CLI --port "$PORT" --objs 10,301 --count 1 --quiet || fail "S4: not answering after the burst"

# ---- S4b: the work bound WELCOME advertises ------------------------------
step "S4b"
# A default-bound server: 10 bodies x 10001 rows is 100010 cells, one row
# over kMaxCellsDefault, and is refused ERROR 2 with the bound named; the
# same request one row under it is answered. (10001 rows, not 20001: the
# rows limit is 20000 and the client refuses more itself.)
PORT1B=$((PORT + 4))
start_server "$PORT1B" "$SCRATCH/a1b.log" --threads 1 \
  || { echo "ROBUST FAIL: S4b: server did not start"; exit 1; }
B1=$SRV_PID
if $CLI --port "$PORT1B" --objs 10,301,199,299,4,5,6,7,8,9 --count 10001 --step 60 \
    --quiet 2> "$SCRATCH/s4b.err"; then
  fail "S4b: a request one row over the default work bound was answered"
elif ! grep -q "server ERROR 2" "$SCRATCH/s4b.err"; then
  fail "S4b: expected ERROR 2, got: $(head -1 "$SCRATCH/s4b.err")"
fi
$CLI --port "$PORT1B" --objs 10,301,199,299,4,5,6,7,8,9 --count 10000 --step 60 \
  --quiet || fail "S4b: a request exactly at the work bound was not answered"
kill "$B1" 2>/dev/null || true

# ---- S9: rows past an ephemeris file's end are NaN, the rest real --------
step "S9"
# The bundled sepl_18 ends in 2399 (measured: JD 2597647 answers, 2597700
# does not): 400 daily rows from 2597500.5 cross it. The rows before the
# edge are real numbers, the rows past it are NaN in all six columns, and
# the object is marked partial (META flag 8) with the rows that computed
# counted -- under protocol 1 the whole object failed and every frame
# before the edge was lost with it. The client's line: idx label errCode
# rowsOk flags columns.
PORT9=$((PORT + 5))
start_server "$PORT9" "$SCRATCH/s9.log" --threads 1 \
  || { echo "ROBUST FAIL: S9: server did not start"; exit 1; }
B9=$SRV_PID
if ! $CLI --port "$PORT9" --objs 10 --jd 2597500.5 --step 86400 --count 400 \
    --out "$SCRATCH/s9.txt" --quiet; then
  fail "S9: the window crossing the file's end was not answered"
else
  real=$(awk '$6 != "nan"' "$SCRATCH/s9.txt" | wc -l)
  nan=$(awk '$6 == "nan"' "$SCRATCH/s9.txt" | wc -l)
  rowsok=$(awk 'NR == 1 {print $4}' "$SCRATCH/s9.txt")
  flags=$(awk 'NR == 1 {print $5}' "$SCRATCH/s9.txt")
  [ "$real" -ge 1 ] || fail "S9: no row before the file's end computed"
  [ "$nan" -ge 1 ] || fail "S9: no row past the file's end is NaN"
  [ "$rowsok" = "$real" ] || fail "S9: META rowsOk $rowsok, but $real rows computed"
  [ $((flags & 8)) -ne 0 ] || fail "S9: the object is not marked partial (flags $flags)"
fi
kill "$B9" 2>/dev/null || true
alive "$A" || fail "S9: the server died on the partial window"

# ---- S6: a second server on the same port --------------------------------
step "S6"
"$ROOT/astrolog-ephd" --port "$PORT" --ephe "$EPH" --cells-per-sec 0 > "$SCRATCH/b.log" 2>&1 &
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
  $CLI --port "$PORT2" --objs 10,301 --jd "$((2451545 + i))" --count 1 --quiet 2>>"$SCRATCH/s5.err" || bad=$((bad + 1))
done
[ "$bad" -eq 0 ] || fail "S5: $bad of 40 connections to a $NT-loop server failed: $(head -1 "$SCRATCH/s5.err")"

# ---- S3: no per-connection leak (last: 20000 connections leave the
step "S3"
# ephemeral range full of TIME_WAIT, which the servers above do not need)
$CLI --port "$PORT2" --cycles 2000 --objs 10 --quiet || fail "S3: warm-up cycles failed"
R0=$(rss_kib "$C")
$CLI --port "$PORT2" --cycles 20000 --objs 10 --quiet || fail "S3: 20000 cycles failed"
R1=$(rss_kib "$C")
GROW=$((R1 - R0))
[ "$GROW" -le 4096 ] || fail "S3: resident memory grew $GROW KiB over 20000 connections"

kill "$C" 2>/dev/null || true

[ "$FAIL" -eq 0 ] || exit 1
echo "ROBUST PASS"
