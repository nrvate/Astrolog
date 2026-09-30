#!/bin/bash
# tools/ephsrv-houses.sh -- the server's house points (object kind 6) against
# the reference implementation of EPHEMERIS_PLUGINS_PLAN.md 3.5b.
#
#   tools/ephsrv-houses.sh              # the gate: starts a server of its own
#   tools/ephsrv-houses.sh --selftest   # the GATE's own decisions, no server
#
# The oracle is tools/houses_ref.py, written from 3.5b's definitions and nothing
# else; the server answers from Swiss's swe_houses_armc_ex2 with three
# overrides (the MC by formula, Topocentric inside the polar circle from its
# definition, Placidus and Koch refused before the call). Every row is graded
# AT ITS OWN REPORTED ARMC AND OBLIQUITY. The grading and the driver are in
# tools/ephsrv_houses.py; see its header for what is asked.
set -euo pipefail
cd "$(dirname "$0")/.."
if [ "${1:-}" = "--selftest" ]; then
  exec python3 tools/ephsrv_houses.py --selftest
fi
ROOT=$PWD
make -s ephsrv
PORT=${PORT:-$((28800 + $$ % 400))}
SCRATCH=$(mktemp -d)
trap 'rm -rf "$SCRATCH"; [ -n "${SRV:-}" ] && kill "$SRV" 2>/dev/null || true' EXIT
./astrolog-ephd --bind 127.0.0.1 --port "$PORT" --threads 2 \
  --ephe "$ROOT/ephem;$ROOT" > "$SCRATCH/ephd.log" 2>&1 &
SRV=$!
for i in $(seq 1 40); do
  grep -q "evt=listen port=" "$SCRATCH/ephd.log" && break
  sleep 0.25
done
grep -q "evt=listen port=" "$SCRATCH/ephd.log" || {
  echo "HOUSES FAIL: server did not start"; cat "$SCRATCH/ephd.log"; exit 1; }
python3 tools/ephsrv_houses.py "$PORT"
