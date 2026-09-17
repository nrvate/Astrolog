#!/bin/bash
# tools/ephsrv-bench.sh -- the ephemeris server's benchmark.
#
# Measures what EPHEMERIS_SERVER_PLAN.md section 9 asks for, with the
# animation window as the unit of work: 30 bodies (Sun..Pluto, the four
# lunar points, Earth, Chiron, Pholus, Ceres, Pallas, Juno, Vesta, the two
# interpolated apsides, and asteroids 5-11) x 1000 rows at 10-minute steps,
# ~14 days of chart time, ~50 seconds of 25 ms animation, 1.4 MiB at f64.
#
#   cold      one client, distinct windows: the compute path, server-side
#             ms from the --verbose log and client-observed round trip
#   hot f64   one client repeating one window: the cache path
#   hot f32   the same at f32, the animation precision
#   hot x N   N concurrent clients on one window: throughput and tail
#   cold x N  N concurrent clients on distinct windows: compute throughput
#             across event loops
#
# Client-observed latency is the microseconds from REQUEST send to the last
# DATA chunk, on loopback, from eph_wsclient --latency; server-side compute
# time is what the server logs per miss. p50/p99 over all samples.
#
# Knobs (env):
#   SWE_HOME  where the thread-safe fork lives  (default /shares/swisseph)
#   EPH       the ephemeris dir handed to --ephe (default: the repo's)
#   PORT      scratch port                      (default 28800 + pid % 100)
#   THREADS   server event loops               (default: the server's, cores)
#   CLIENTS   concurrent clients               (default 8)
#   REPEAT    hot repeats per client           (default 50)
#   COLD      cold windows per client          (default 5)
#   TLS       1 to run every scenario over wss:// against a throwaway CA,
#             so TLS's cost is a number beside the plain one
#
# Prints one table; exits nonzero only if a request fails. Numbers from the
# run that wrote this are in the plan's work log, item 7.

set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
SWE_HOME=${SWE_HOME:-/shares/swisseph}
EPH=${EPH:-$ROOT/ephem}
PORT=${PORT:-$((28800 + $$ % 100))}   # below the ephemeral range: ephsrv-robust.sh says why
CLIENTS=${CLIENTS:-8}
REPEAT=${REPEAT:-50}
COLD=${COLD:-5}
SCRATCH=$(mktemp -d /tmp/ephsrv-bench.XXXXXX)
EPHD_PID=
LOG="$SCRATCH/ephd.log"

cleanup() {
  [ -n "$EPHD_PID" ] && kill "$EPHD_PID" 2>/dev/null
  rm -rf "$SCRATCH"
}
trap cleanup EXIT

[ -x "$ROOT/astrolog-ephd" ] && [ -x "$ROOT/eph_wsclient" ] || make -s ephsrv >/dev/null

THREAD_ARG=()
[ -n "${THREADS:-}" ] && THREAD_ARG=(--threads "$THREADS")
TLS_SRV=() TLS_CLI=()
if [ "${TLS:-0}" = 1 ]; then
  (cd "$SCRATCH" &&
   openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes \
     -keyout ca.key -out ca.pem -days 2 -subj /CN=ephsrv-bench-ca &&
   openssl req -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes \
     -keyout srv.key -out srv.csr -subj /CN=localhost &&
   printf 'subjectAltName=DNS:localhost,IP:127.0.0.1\n' > ext.cnf &&
   openssl x509 -req -in srv.csr -CA ca.pem -CAkey ca.key -CAcreateserial \
     -out srv.pem -days 1 -extfile ext.cnf) > "$SCRATCH/openssl.log" 2>&1 ||
    { echo "BENCH FAIL: could not make the test certificates"; exit 1; }
  TLS_SRV=(--tls-cert "$SCRATCH/srv.pem" --tls-key "$SCRATCH/srv.key")
  TLS_CLI=(--tls --ca "$SCRATCH/ca.pem")
fi
# No compute budget: a bench measures the server, not its rate limit.
"$ROOT/astrolog-ephd" --port "$PORT" --ephe "$EPH" --verbose --cells-per-sec 0 "${THREAD_ARG[@]}" \
  "${TLS_SRV[@]}" > "$LOG" 2>&1 &
EPHD_PID=$!
for i in $(seq 1 50); do
  grep -q "evt=listen port=" "$LOG" 2>/dev/null && break
  sleep 0.1
done
grep -q "evt=listen port=" "$LOG" || { echo "BENCH FAIL: server did not start"; exit 1; }
LOOPS=$(sed -nE 's/.* evt=config .* loops=([0-9]+).*/\1/p' "$LOG")

BODIES="0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,10005,10006,10007,10008,10009,10010,10011"
ROWS=1000
STEP=600
JD0=2451545.0

ask() {   # ask <latency-file> <jd> <extra args...>
  local lat=$1 jd=$2; shift 2
  "$ROOT/eph_wsclient" "${TLS_CLI[@]}" --port "$PORT" --objs "$BODIES" --jd "$jd" --step $STEP \
    --count $ROWS --quiet --latency "$lat" "$@" \
    || { echo "BENCH FAIL: request failed at jd $jd: $*"; exit 1; }
}

# Percentiles over a file of one number per line; prints "p50 p99 n".
pct() {
  python3 - "$1" << 'EOF'
import sys
xs = sorted(float(l) for l in open(sys.argv[1]) if l.strip())
def p(q):
    if not xs: return float('nan')
    i = min(len(xs) - 1, int(round(q * (len(xs) - 1))))
    return xs[i]
print("%.2f %.2f %d" % (p(0.5) / 1000.0, p(0.99) / 1000.0, len(xs)))
EOF
}

# 1. cold: COLD distinct windows, one client at a time. Distinct jds so
#    every one is a miss; the server-side ms come from the log.
: > "$SCRATCH/cold.lat"
for i in $(seq 0 $((COLD - 1))); do
  ask "$SCRATCH/cold.lat" "$(python3 -c "print($JD0 + $i * 100)")"
done
grep -oE "cache=miss compute_ms=[0-9.]+" "$LOG" | awk -F= '{print $3 * 1000}' > "$SCRATCH/cold.srv"
read -r cold_p50 cold_p99 cold_n < <(pct "$SCRATCH/cold.lat")
read -r cold_s50 cold_s99 _ < <(pct "$SCRATCH/cold.srv")

# 2. hot f64: one window, REPEAT times on one connection. The cache is
#    per event loop and a new connection lands on whichever loop the
#    kernel picks, so the connection's FIRST request may be a miss there
#    even though another loop has the window; it is dropped from the
#    sample (that is also why a client keeps one connection for its
#    lifetime -- its loop's cache is the one that serves it).
: > "$SCRATCH/hot64.lat"
ask "$SCRATCH/hot64.raw" "$JD0" --repeat $((REPEAT + 1))
tail -n +2 "$SCRATCH/hot64.raw" > "$SCRATCH/hot64.lat"
read -r h64_p50 h64_p99 h64_n < <(pct "$SCRATCH/hot64.lat")

# 3. hot f32: same window, f32 delivery, same first-request rule.
ask "$SCRATCH/hot32.raw" "$JD0" --repeat $((REPEAT + 1)) --precision 32
tail -n +2 "$SCRATCH/hot32.raw" > "$SCRATCH/hot32.lat"
read -r h32_p50 h32_p99 h32_n < <(pct "$SCRATCH/hot32.lat")

# 4. hot x CLIENTS: every client repeats the same window concurrently;
#    the first request of each connection is dropped as above.
: > "$SCRATCH/hotN.lat"
# "wait" with no arguments would wait for the server too (it is a
# background job of this shell and never exits), so the client PIDs are
# named. That is how the first run of this script hung.
PIDS=()
T0=$(date +%s.%N)
for c in $(seq 1 "$CLIENTS"); do
  ( "$ROOT/eph_wsclient" "${TLS_CLI[@]}" --port "$PORT" --objs "$BODIES" --jd "$JD0" --step $STEP \
      --count $ROWS --quiet --latency "$SCRATCH/hotN.$c.lat" --repeat $((REPEAT + 1)) \
      || { echo "BENCH FAIL: concurrent hot client $c failed" | tee -a "$SCRATCH/fail"; } ) &
  PIDS+=($!)
done
wait "${PIDS[@]}"
T1=$(date +%s.%N)
for c in $(seq 1 "$CLIENTS"); do tail -n +2 "$SCRATCH/hotN.$c.lat" >> "$SCRATCH/hotN.lat"; done
read -r hN_p50 hN_p99 hN_n < <(pct "$SCRATCH/hotN.lat")
hN_rps=$(python3 -c "print('%.0f' % ($hN_n / ($T1 - $T0)))")

# 5. cold x CLIENTS: every client its own run of COLD distinct windows,
#    concurrently -- the compute path across loops. Each client is a new
#    connection per window, so SO_REUSEPORT's hashing decides how evenly
#    the CLIENTS spread over the loops; two on one loop queue behind each
#    other, which is what the p99 here measures.
: > "$SCRATCH/coldN.lat"
PIDS=()
T0=$(date +%s.%N)
for c in $(seq 1 "$CLIENTS"); do
  (
    for i in $(seq 0 $((COLD - 1))); do
      jd=$(python3 -c "print($JD0 + 10000 + $c * 1000 + $i * 100)")
      "$ROOT/eph_wsclient" "${TLS_CLI[@]}" --port "$PORT" --objs "$BODIES" --jd "$jd" --step $STEP \
        --count $ROWS --quiet --latency "$SCRATCH/coldN.$c.lat" \
        || { echo "BENCH FAIL: concurrent cold client $c failed" | tee -a "$SCRATCH/fail"; }
    done
  ) &
  PIDS+=($!)
done
wait "${PIDS[@]}"
T1=$(date +%s.%N)
cat "$SCRATCH"/coldN.*.lat >> "$SCRATCH/coldN.lat"
read -r cN_p50 cN_p99 cN_n < <(pct "$SCRATCH/coldN.lat")
cN_wps=$(python3 -c "print('%.1f' % ($cN_n / ($T1 - $T0)))")

# A concurrent client's failure is written to $SCRATCH/fail by its own
# subshell. This grepped the SERVER log for the word, which no client ever
# wrote there, so a failed concurrent client printed a table of blanks and
# exited 0 (EPHEMERIS_REVIEW.md T7).
[ ! -s "$SCRATCH/fail" ] || { echo "BENCH FAIL: $(wc -l < "$SCRATCH/fail") concurrent client(s) failed"; exit 1; }
for f in cold hot64 hot32 hotN coldN; do
  [ -s "$SCRATCH/$f.lat" ] || { echo "BENCH FAIL: no latency samples for $f"; exit 1; }
done

echo "ephsrv bench: $LOOPS event loop(s), window = 30 bodies x $ROWS rows @ ${STEP}s, $(nproc) cores"
echo
printf '%-26s %10s %10s %8s  %s\n' "scenario" "p50 ms" "p99 ms" "n" "note"
printf '%-26s %10s %10s %8s  %s\n' "cold, server compute" "$cold_s50" "$cold_s99" "$cold_n" "swe_calc_ut_r x 30,000 per window"
printf '%-26s %10s %10s %8s  %s\n' "cold, client round trip" "$cold_p50" "$cold_p99" "$cold_n" "one client, distinct windows"
printf '%-26s %10s %10s %8s  %s\n' "hot f64, client" "$h64_p50" "$h64_p99" "$h64_n" "one client, 1.4 MiB per window"
printf '%-26s %10s %10s %8s  %s\n' "hot f32, client" "$h32_p50" "$h32_p99" "$h32_n" "one client, 0.7 MiB per window"
printf '%-26s %10s %10s %8s  %s\n' "hot f64 x $CLIENTS clients" "$hN_p50" "$hN_p99" "$hN_n" "$hN_rps windows/s aggregate"
printf '%-26s %10s %10s %8s  %s\n' "cold x $CLIENTS clients" "$cN_p50" "$cN_p99" "$cN_n" "$cN_wps windows/s aggregate"
