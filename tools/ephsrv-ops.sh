#!/bin/bash
# tools/ephsrv-ops.sh -- the ephemeris server's operations gate.
#
# What running it as a service needs (EPHEMERIS_SERVER_PRODUCTION_PLAN.md
# Phase 2), each asserted:
#
#   routes   /healthz answers ok; /readyz answers ready, and 503 with the
#            reason on a server that found no ephemeris; /metrics is
#            Prometheus text whose counters MOVE -- a computed request, a
#            cache hit and an ERROR 2 each change exactly what they should;
#            the routes answer over https too
#   privacy  nothing in the server's log names a request's instant
#   drain    SIGTERM with a slow reader mid-answer: new connections are
#            refused, the reader gets every row, the server exits 0; a
#            reader that never reads is cut off at --drain-seconds and the
#            server still exits 0, saying so; a second signal exits at once
#
# Knobs (env): EPH, PORT (default 28700 + pid % 80, and PORT + 1..3; below
# the ephemeral range, ephsrv-robust.sh says why).
#
# Exit 0 with "OPS PASS"; nonzero with the first failure named.

set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
EPH=${EPH:-$ROOT/ephem}
PORT=${PORT:-$((28700 + $$ % 80))}
SCRATCH=$(mktemp -d /tmp/ephsrv-ops.XXXXXX)
PIDS=()
CLI="$ROOT/eph_wsclient"
S="$SCRATCH"

# A kill of a process that already exited fails, and under set -e the LAST
# command of an && list still stops the script: a passing run exited 1.
cleanup() {
  for p in "${PIDS[@]:-}"; do [ -n "$p" ] && { kill "$p" 2>/dev/null || true; }; done
  rm -rf "$SCRATCH"
  true
}
trap cleanup EXIT

fail() { echo "OPS FAIL: $*"; for f in "$S"/*.log; do [ -f "$f" ] && sed "s|^|  $(basename "$f"): |" "$f" | tail -6; done; exit 1; }

[ -x "$ROOT/astrolog-ephd" ] && [ -x "$CLI" ] || make -s ephsrv >/dev/null
command -v curl >/dev/null || fail "no curl on PATH"

SRV_PID=
start() {   # start PORT LOG ARGS...
  local port=$1 log=$2; shift 2
  "$ROOT/astrolog-ephd" --port "$port" --threads 2 "$@" > "$log" 2>&1 &
  SRV_PID=$!
  PIDS+=("$SRV_PID")
  for _ in $(seq 1 50); do
    grep -q "listening on port" "$log" 2>/dev/null && return 0
    kill -0 "$SRV_PID" 2>/dev/null || return 1
    sleep 0.1
  done
  return 1
}
metric() {   # metric PORT NAME -> value, summed over label sets
  curl -s "http://127.0.0.1:$1/metrics" |
    awk -v n="$2" '$1 == n || index($1, n "{") == 1 { s += $2 } END { print s + 0 }'
}
waitexit() {   # waitexit PID SECONDS -> the exit code, or fail
  local pid=$1 t=$2 i
  for i in $(seq 1 $((t * 10))); do
    kill -0 "$pid" 2>/dev/null || { wait "$pid"; return $?; }
    sleep 0.1
  done
  fail "the server did not exit within $t s"
}

# -- routes ----------------------------------------------------------------
# --verbose: the privacy check below reads the most the log ever says.
start "$PORT" "$S/a.log" --ephe "$EPH" --verbose || fail "server did not start"
[ "$(curl -s "http://127.0.0.1:$PORT/healthz")" = ok ] || fail "/healthz is not ok"
code=$(curl -s -o "$S/ready.txt" -w '%{http_code}' "http://127.0.0.1:$PORT/readyz")
[ "$code" = 200 ] && grep -q ready "$S/ready.txt" || fail "/readyz: $code $(cat "$S/ready.txt")"
curl -s "http://127.0.0.1:$PORT/metrics" > "$S/m0.txt"
grep -q '^ephd_build_info{server="astrolog-ephd/' "$S/m0.txt" || fail "/metrics has no build info"
grep -q '^# TYPE ephd_compute_seconds histogram' "$S/m0.txt" || fail "/metrics has no compute histogram"
echo "  routes  /healthz, /readyz and /metrics answer"

r0=$(metric "$PORT" ephd_requests_total); c0=$(metric "$PORT" ephd_cells_computed_total)
h0=$(metric "$PORT" ephd_cache_hits_total); e0=$(metric "$PORT" 'ephd_errors_total{code="2"}')
# Both on ONE connection: each loop keeps its own cache, and a second
# connection may land on the other loop and miss (it did, one run in two).
"$CLI" --port "$PORT" --objs 0,1,2 --jd 2451545.0 --count 7 --repeat 2 --quiet ||
  fail "the request and its repeat failed"
[ "$(metric "$PORT" ephd_requests_total)" = $((r0 + 2)) ] || fail "requests_total did not move by 2"
[ "$(metric "$PORT" ephd_cells_computed_total)" = $((c0 + 21)) ] ||
  fail "cells_computed_total did not move by exactly 3 x 7 (a hit counted as computed?)"
[ "$(metric "$PORT" ephd_cache_hits_total)" = $((h0 + 1)) ] || fail "cache_hits_total did not move on the repeat"
"$CLI" --port "$PORT" --objs 0,1,2,3,4,5,6 --count 20000 --quiet 2> /dev/null && fail "140000 cells were not refused"
[ "$(metric "$PORT" 'ephd_errors_total{code="2"}')" = $((e0 + 1)) ] || fail "errors_total{code=2} did not move"
echo "  routes  the counters move: a computed request, a cache hit, an ERROR 2"

grep -q 2451545 "$S/a.log" && fail "the log names a request's instant"
echo "  privacy the log names no request's instant"
kill "$SRV_PID"; waitexit "$SRV_PID" 15 || true

mkdir -p "$S/empty"
start "$((PORT + 1))" "$S/b.log" --ephe "$S/empty" || fail "the no-ephemeris server did not start"
code=$(curl -s -o "$S/ready.txt" -w '%{http_code}' "http://127.0.0.1:$((PORT + 1))/readyz")
[ "$code" = 503 ] && grep -q "no ephemeris" "$S/ready.txt" || fail "/readyz with no ephemeris: $code $(cat "$S/ready.txt")"
echo "  routes  /readyz is 503 and says why with no ephemeris"
kill "$SRV_PID"; waitexit "$SRV_PID" 15 || true

if command -v openssl >/dev/null; then
  (cd "$S" &&
   openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes \
     -keyout ca.key -out ca.pem -days 2 -subj /CN=ephsrv-ops-ca &&
   openssl req -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes \
     -keyout srv.key -out srv.csr -subj /CN=localhost &&
   printf 'subjectAltName=DNS:localhost,IP:127.0.0.1\n' > ext.cnf &&
   openssl x509 -req -in srv.csr -CA ca.pem -CAkey ca.key -CAcreateserial \
     -out srv.pem -days 1 -extfile ext.cnf) > "$S/openssl.out" 2>&1 || fail "openssl failed"
  start "$((PORT + 2))" "$S/c.log" --ephe "$EPH" --tls-cert "$S/srv.pem" --tls-key "$S/srv.key" ||
    fail "the TLS server did not start"
  [ "$(curl -s --cacert "$S/ca.pem" "https://localhost:$((PORT + 2))/healthz")" = ok ] ||
    fail "/healthz over https"
  echo "  routes  the routes answer over https"
  kill "$SRV_PID"; waitexit "$SRV_PID" 15 || true
fi

# -- drain -----------------------------------------------------------------
P3=$((PORT + 3))
start "$P3" "$S/d.log" --ephe "$EPH" || fail "the drain server did not start"
# 100000 cells of f64 is ~4.8 MB, past the 4 MB a stream may buffer before it
# waits: a reader asleep 1.5 s leaves the answer half sent when SIGTERM lands.
"$CLI" --port "$P3" --objs 0,1,2,3,4 --count 20000 --step 600 --sleep-ms 1500 \
  --quiet --out "$S/long.txt" 2> "$S/long.err" &
LONG=$!
sleep 0.8
kill -TERM "$SRV_PID"
sleep 0.3
"$CLI" --port "$P3" --objs 0 --count 1 --quiet 2> /dev/null && fail "a new connection was accepted while draining"
wait "$LONG" || fail "the slow reader failed during the drain: $(cat "$S/long.err")"
[ "$(wc -l < "$S/long.txt")" = 100000 ] || fail "the slow reader got $(wc -l < "$S/long.txt") of 100000 rows"
waitexit "$SRV_PID" 15 || fail "the drained server exited nonzero"
grep -q "drained; exiting" "$S/d.log" || fail "the drain did not say it finished"
echo "  drain   new connections refused, the answer in flight delivered whole, exit 0"

start "$P3" "$S/e.log" --ephe "$EPH" --drain-seconds 1 || fail "the deadline server did not start"
"$CLI" --port "$P3" --objs 0,1,2,3,4 --count 20000 --step 600 --sleep-ms 20000 \
  --quiet > /dev/null 2>&1 &
PIDS+=($!)
sleep 0.8
kill -TERM "$SRV_PID"
waitexit "$SRV_PID" 5 || fail "the server cut off at the deadline exited nonzero"
grep -q "still had answers unsent" "$S/e.log" || fail "the deadline drain did not say what it cut off"
echo "  drain   a reader that never reads is cut off at --drain-seconds, exit 0"

start "$P3" "$S/f.log" --ephe "$EPH" --drain-seconds 60 || fail "the second-signal server did not start"
"$CLI" --port "$P3" --objs 0,1,2,3,4 --count 20000 --step 600 --sleep-ms 20000 \
  --quiet > /dev/null 2>&1 &
PIDS+=($!)
sleep 0.8
kill -TERM "$SRV_PID"; sleep 0.3; kill -TERM "$SRV_PID"
set +e; waitexit "$SRV_PID" 3; rc=$?; set -e
[ "$rc" = 1 ] || fail "a second signal while draining exited $rc, wanted 1 at once"
echo "  drain   a second signal exits at once"

echo "OPS PASS"
