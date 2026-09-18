#!/bin/bash
# tools/ephsrv-load.sh -- load the ephemeris server the way Astrolog does,
# and measure what it costs.
#
# Not a gate: a measuring instrument, like ephsrv-bench.sh, for the sizing
# EPHEMERIS_SERVER_PRODUCTION_PLAN.md Phase 6 asks for and the decision
# Phase 7 waits on (whether computing on the event loops stalls anyone).
#
# Two kinds of client, run together for DURATION seconds:
#   animators  each asks, back to back, the Qt client's animation window --
#              BODIES x 1000 rows, f32 -- at a new instant every time, so
#              every window is computed (the worst case: no cache hits)
#   casters    each asks a still chart -- BODIES x 1 row, f64 -- at a random
#              instant, back to back
# and reports, per kind, requests completed and p50/p99 latency; the
# server's CPU (all loops together, in cores) and peak RSS; and ERRORs by
# code from /metrics. The casters' p99 while animators run is the number
# Phase 7 is about: a still chart waiting behind a window on its loop.
#
# Knobs (env): ANIMATORS (4), CASTERS (8), DURATION (20), THREADS (the
# server's default, cores), BODIES (30), PORT (26800 + pid % 90), EPH, and
# URL=ws://host:port to load a server somewhere else instead of starting
# one (then CPU and RSS are not reported). The server is started with
# --cells-per-sec 0 and --max-conns-per-ip 0: this measures capacity, not
# the limits, which would refuse a load test from one address by design.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
EPH=${EPH:-$ROOT/ephem}
ANIMATORS=${ANIMATORS:-4}
CASTERS=${CASTERS:-8}
DURATION=${DURATION:-20}
BODIES=${BODIES:-30}
PORT=${PORT:-$((26800 + $$ % 90))}
SCRATCH=$(mktemp -d /tmp/ephsrv-load.XXXXXX)
CLI="$ROOT/eph_wsclient"
PIDS=()
EPHD_PID=

cleanup() {
  for p in "${PIDS[@]:-}"; do [ -n "$p" ] && { kill "$p" 2>/dev/null || true; }; done
  [ -n "$EPHD_PID" ] && { kill "$EPHD_PID" 2>/dev/null || true; }
  rm -rf "$SCRATCH"
  true
}
trap cleanup EXIT

[ -x "$ROOT/astrolog-ephd" ] && [ -x "$CLI" ] || make -s ephsrv > /dev/null

HOST=127.0.0.1
if [ -n "${URL:-}" ]; then
  HOST=$(echo "$URL" | sed -E 's|^wss?://||; s|:[0-9]+.*||')
  PORT=$(echo "$URL" | sed -nE 's|.*:([0-9]+).*|\1|p')
  case "$URL" in wss://*) TLS=(--tls) ;; *) TLS=() ;; esac
else
  TLS=()
  THREAD_ARG=()
  [ -n "${THREADS:-}" ] && THREAD_ARG=(--threads "$THREADS")
  "$ROOT/astrolog-ephd" --port "$PORT" --ephe "$EPH" --cells-per-sec 0 \
    --max-conns-per-ip 0 "${THREAD_ARG[@]}" > "$SCRATCH/ephd.log" 2>&1 &
  EPHD_PID=$!
  for _ in $(seq 1 50); do grep -q "evt=listen port=" "$SCRATCH/ephd.log" && break; sleep 0.1; done
  grep -q "evt=listen port=" "$SCRATCH/ephd.log" || { echo "LOAD FAIL: server did not start"; exit 1; }
fi

IDS=$(python3 -c "b=[10,301,199,299,4,5,6,7,8,9,399,20002060,20005145,20000001,20000002,20000003,20000004]+list(range(20000005,20000100)); print(','.join(map(str,b[:$BODIES])))")
LOOPS=$(sed -nE 's/.* evt=config .* loops=([0-9]+).*/\1/p' "$SCRATCH/ephd.log" 2>/dev/null || true)

cpu_ticks() { awk '{print $14 + $15}' "/proc/$EPHD_PID/stat"; }
T_END=$(( $(date +%s) + DURATION ))

animator() {   # animator N
  local jd=$((2400000 + $1 * 100000))
  while [ "$(date +%s)" -lt "$T_END" ]; do
    "$CLI" "${TLS[@]}" --host "$HOST" --port "$PORT" --objs "$IDS" --jd "$jd.5" \
      --step 600 --count 1000 --precision 32 --quiet \
      --latency "$SCRATCH/anim.$1" 2>> "$SCRATCH/errors" || true
    jd=$((jd + 7))
  done
}
caster() {   # caster N
  local i=0
  while [ "$(date +%s)" -lt "$T_END" ]; do
    "$CLI" "${TLS[@]}" --host "$HOST" --port "$PORT" --objs "$IDS" \
      --jd "$((2378500 + (RANDOM * 32768 + RANDOM) % 219000)).$((i % 10))" \
      --count 1 --quiet --latency "$SCRATCH/cast.$1" 2>> "$SCRATCH/errors" || true
    i=$((i + 1))
  done
}

[ -n "$EPHD_PID" ] && { T0=$(cpu_ticks); }
W0=$(date +%s.%N)
for n in $(seq 1 "$ANIMATORS"); do animator "$n" & PIDS+=($!); done
for n in $(seq 1 "$CASTERS"); do caster "$n" & PIDS+=($!); done
RSS=0
while [ "$(date +%s)" -lt "$T_END" ]; do
  if [ -n "$EPHD_PID" ]; then
    r=$(awk '/^VmRSS/ {print $2}' "/proc/$EPHD_PID/status")
    [ "${r:-0}" -gt "$RSS" ] && RSS=$r
  fi
  sleep 0.5
done
wait "${PIDS[@]}" 2>/dev/null || true
PIDS=()
W1=$(date +%s.%N)
[ -n "$EPHD_PID" ] && { T1=$(cpu_ticks); }

cat "$SCRATCH"/anim.* 2>/dev/null > "$SCRATCH/anim.all" || true
cat "$SCRATCH"/cast.* 2>/dev/null > "$SCRATCH/cast.all" || true
curl -s "http://$HOST:$PORT/metrics" > "$SCRATCH/metrics" 2>/dev/null || true

python3 - "$SCRATCH" "$W0" "$W1" "${T0:-0}" "${T1:-0}" "$(getconf CLK_TCK)" "$RSS" \
  "$ANIMATORS" "$CASTERS" "$BODIES" "${LOOPS:-?}" << 'PY'
import os, sys
s, w0, w1, t0, t1, hz, rss, na, nc, nb, loops = sys.argv[1:]
wall = float(w1) - float(w0)
def load(name):
    try:
        return sorted(int(x) / 1000.0 for x in open(os.path.join(s, name)).read().split())
    except FileNotFoundError:
        return []
def pct(v, p):
    return v[min(len(v) - 1, int(len(v) * p))] if v else float("nan")
print("ephsrv load: %s animators (%s x 1000 f32), %s casters (%s x 1), %.0f s, %s loop(s)"
      % (na, nb, nc, nb, wall, loops))
print("%-10s %8s %8s %10s %10s" % ("", "done", "per s", "p50 ms", "p99 ms"))
for label, name in (("windows", "anim.all"), ("charts", "cast.all")):
    v = load(name)
    print("%-10s %8d %8.1f %10.1f %10.1f" % (label, len(v), len(v) / wall, pct(v, .5), pct(v, .99)))
if int(t1):
    print("server CPU %.2f cores, peak RSS %.0f MB" % ((int(t1) - int(t0)) / float(hz) / wall, int(rss) / 1024.0))
errs = [l for l in open(os.path.join(s, "metrics")).read().splitlines()
        if l.startswith("ephd_errors_total") and not l.endswith(" 0")] if os.path.exists(os.path.join(s, "metrics")) else []
try:
    failed = open(os.path.join(s, "errors")).read().count("\n")
except FileNotFoundError:
    failed = 0
print("ERRORs: %s; client failures: %d" % (", ".join(errs) if errs else "none", failed))
PY
