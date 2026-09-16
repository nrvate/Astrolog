#!/bin/bash
# tools/ephsrv-golden.sh -- the ephemeris server's golden gate.
#
# Starts astrolog-ephd on a scratch port, fetches a fixed sample of bodies
# and instants through eph_wsclient, and compares every column bit-exact
# (hexfloat string equality) against the same Swiss Ephemeris the server
# itself links: a tiny probe compiled here from $(SWE_HOME)/libswe with the
# same forced flags (SEFLG_SWIEPH|SEFLG_SPEED), sidereal via
# swe_set_sid_mode, same epoch handling (swe_calc_ut). swetest is not the
# oracle because its output is decimal and rounded; the probe prints raw
# IEEE bits, so agreement means the server's answers ARE local Swiss
# answers, not close to them.
#
# Knobs (env):
#   SWE_HOME   where the thread-safe fork lives  (default /shares/swisseph)
#   EPH            the ephemeris dir handed to --ephe (default: the repo's)
#   PORT           scratch port                  (default 47200 + pid % 400)
#   KEEP           set to keep the server running after the gate
#
# Exit 0 with "GOLDEN PASS" when every column matches; nonzero with the
# first mismatch diff otherwise. Cleans up its server and scratch files.

set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
SWE_HOME=${SWE_HOME:-/shares/swisseph}
EPH=${EPH:-$ROOT/ephem}
PORT=${PORT:-$((47200 + $$ % 400))}
SCRATCH=$(mktemp -d /tmp/ephsrv-golden.XXXXXX)
EPHD_PID=

cleanup() {
  [ -n "$EPHD_PID" ] && kill "$EPHD_PID" 2>/dev/null
  rm -rf "$SCRATCH"
}
trap cleanup EXIT

[ -x "$ROOT/astrolog-ephd" ] || make -s ephsrv >/dev/null
[ -x "$ROOT/eph_wsclient" ] || make -s ephsrv >/dev/null

# The oracle probe: same library, same flags, same entry points as the
# server's ComputeCell. One line of hexfloat per answer: lon lat dist
# slon slat sdist, exactly the columns the wire carries.
cat > "$SCRATCH/oracle.c" << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include "swephexp.h"
int main(int argc, char **argv) {
  /* argv: jd ipl iflagHex sidMode ephePath */
  double jd = atof(argv[1]);
  int ipl = atoi(argv[2]);
  int32 iflag = (int32)strtoul(argv[3], NULL, 16) | SEFLG_SWIEPH | SEFLG_SPEED;
  int sidMode = atoi(argv[4]);
  char serr[256];
  double xx[6];
  /* The context API, exactly the server's path: the process-global one
     config (ephe path) published, then a fresh context that inherits it
     and answers through swe_calc_ut_r. The legacy swe_calc_ut diverges
     from this at historical dates (the delta-t table's init is one-shot
     and per-process; see the fork's notes/UPSTREAM-BUGS.md), and the
     server's contract is the context path, so the oracle calls the same. */
  swe_set_ephe_path(argv[5]);
  swe_ctx *ctx = swe_ctx_new();
  if (iflag & SEFLG_SIDEREAL)
    swe_set_sid_mode_r(ctx, sidMode, 0.0, 0.0);
  int32 ret = swe_calc_ut_r(ctx, jd, ipl, iflag, xx, serr);
  if (ret < 0) { printf("ERR %s\n", serr); return 1; }
  printf("%a %a %a %a %a %a\n", xx[0], xx[1], xx[2], xx[3], xx[4], xx[5]);
  return 0;
}
EOF
# Static archive, not -lswe: an installed libswe.so (this machine keeps a
# stale one in /usr/local/lib) wins the runtime search over -L, and the
# oracle would then answer from a different library than the server.
gcc -O2 -I"$SWE_HOME" "$SCRATCH/oracle.c" "$SWE_HOME/libswe.a" -lm -ldl \
  -lpthread -o "$SCRATCH/oracle"

"$ROOT/astrolog-ephd" --port "$PORT" --ephe "$EPH" --threads 1 \
  > "$SCRATCH/ephd.log" 2>&1 &
EPHD_PID=$!
for i in $(seq 1 50); do
  grep -q "ephemeris path" "$SCRATCH/ephd.log" 2>/dev/null && break
  sleep 0.1
done

# The sample: Sun..Pluto, the Moon, and numbered asteroid 4 (Vesta, a real
# file in every tree), across six instants spanning the sepl_18 century.
BODIES="0 1 2 3 4 5 6 7 8 9 10004"
# Inside sepl_18's 1800-2399 coverage on purpose: 2378490.5 sits 0.0002
# days before the file's boundary and both ends would refuse it.
JDs="2415020.5 2433895.5 2451545.0 2465000.5 2378500.5 2489500.5"
FAIL=0
TRIED=0
for jd in $JDs; do
  ids=$(echo $BODIES | tr ' ' ',')
  if ! "$ROOT/eph_wsclient" --port "$PORT" --objs "$ids" --jd "$jd" \
      --step 600 --count 1 --out "$SCRATCH/got.txt" --quiet; then
    echo "GOLDEN FAIL: request for JD $jd failed"; exit 1
  fi
  # got.txt: "<objIdx> <sweId> <retFlag> <6 hex>" per body, one row.
  n=0
  while read -r idx who retFlag lon lat dist slon slat sdist; do
    ipl=$who
    if ! oracle=$("$SCRATCH/oracle" "$jd" "$ipl" "0" "0" "$EPH"); then
      echo "GOLDEN FAIL: oracle refused jd=$jd ipl=$ipl: $oracle"
      exit 1
    fi
    got="$lon $lat $dist $slon $slat $sdist"
    TRIED=$((TRIED + 1))
    if [ "$got" != "$oracle" ]; then
      FAIL=$((FAIL + 1))
      if [ "$FAIL" -le 3 ]; then
        echo "MISMATCH jd=$jd ipl=$ipl"
        echo "  server: $got"
        echo "  oracle: $oracle"
      fi
    fi
    n=$((n + 1))
  done < "$SCRATCH/got.txt"
  [ "$n" -eq 11 ] || { echo "GOLDEN FAIL: JD $jd returned $n of 11 rows"; exit 1; }
done

# Sidereal: Fagan-Bradley (mode 0) through both ends at one instant.
for jd in 2451545.0 2400000.5; do
  "$ROOT/eph_wsclient" --port "$PORT" --objs 0,1 --jd "$jd" --step 600 \
    --count 1 --iflag 10000 --sid "0,0,0" --out "$SCRATCH/sid.txt" --quiet
  while read -r idx who retFlag lon lat dist slon slat sdist; do
    ipl=$who
    if ! oracle=$("$SCRATCH/oracle" "$jd" "$ipl" "10000" "0" "$EPH"); then
      echo "GOLDEN FAIL: sidereal oracle refused jd=$jd ipl=$ipl: $oracle"
      exit 1
    fi
    got="$lon $lat $dist $slon $slat $sdist"
    TRIED=$((TRIED + 1))
    if [ "$got" != "$oracle" ]; then
      FAIL=$((FAIL + 1))
      echo "SIDEREAL MISMATCH jd=$jd ipl=$ipl"
      echo "  server: $got"
      echo "  oracle: $oracle"
    fi
  done < "$SCRATCH/sid.txt"
done

if [ "$FAIL" -gt 0 ]; then
  echo "GOLDEN FAIL: $FAIL of $TRIED columns mismatched"
  exit 1
fi
echo "GOLDEN PASS: $TRIED columns bit-exact against $SWE_HOME"
