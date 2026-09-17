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
#   PORT           scratch port                  (default 28000 + pid % 400)
#   KEEP           set to keep the server running after the gate
#
# Exit 0 with "GOLDEN PASS" when every column matches; nonzero with the
# first mismatch diff otherwise. Cleans up its server and scratch files.

set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
SWE_HOME=${SWE_HOME:-/shares/swisseph}
EPH=${EPH:-$ROOT/ephem}
PORT=${PORT:-$((28000 + $$ % 400))}   # below the ephemeral range: ephsrv-robust.sh says why
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
#include <string.h>
#include "swephexp.h"
int main(int argc, char **argv) {
  /* argv: jd ipl iflagHex sidMode ephePath [mode [topoLon topoLat topoAlt]]
     mode: ut (default) swe_calc_ut_r; tt swe_calc_r at a TT instant;
     nodaps:P:M swe_nod_aps_r at TT, point P 1-4, method M 0 mean 1 oscu;
     pctr:C swe_calc_pctr_r at TT centered on body C.
     The three trailing args, taken when iflag carries SEFLG_TOPOCTR, are
     the observer the server's request carried (EPHEMERIS_REVIEW.md T8). */
  double jd = atof(argv[1]);
  int ipl = atoi(argv[2]);
  int32 iflag = (int32)strtoul(argv[3], NULL, 16) | SEFLG_SWIEPH | SEFLG_SPEED;
  int sidMode = atoi(argv[4]);
  const char *mode = argc > 6 ? argv[6] : "ut";
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
  if (iflag & SEFLG_TOPOCTR) {
    if (argc < 10) { printf("ERR topo needs lon lat alt\n"); return 1; }
    swe_set_topo_r(ctx, atof(argv[7]), atof(argv[8]), atof(argv[9]));
  }
  int32 ret;
  if (strncmp(mode, "nodaps:", 7) == 0) {
    int pnt = 0, meth = 0;
    double xn[6], xd[6], xp[6], xa[6], *px;
    sscanf(mode + 7, "%d:%d", &pnt, &meth);
    ret = swe_nod_aps_r(ctx, jd, ipl, iflag, meth ? SE_NODBIT_OSCU : SE_NODBIT_MEAN,
                        xn, xd, xp, xa, serr);
    px = pnt == 1 ? xn : pnt == 2 ? xd : pnt == 3 ? xp : xa;
    for (int i = 0; i < 6; i++) xx[i] = px[i];
  } else if (strncmp(mode, "pctr:", 5) == 0) {
    ret = swe_calc_pctr_r(ctx, jd, ipl, atoi(mode + 5), iflag, xx, serr);
  } else if (strcmp(mode, "tt") == 0) {
    ret = swe_calc_r(ctx, jd, ipl, iflag, xx, serr);
  } else {
    ret = swe_calc_ut_r(ctx, jd, ipl, iflag, xx, serr);
  }
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
  grep -q "listening on port" "$SCRATCH/ephd.log" 2>/dev/null && break
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

# The ET entry points behind kIflagTimeTT (the Astrolog client's path: it
# computes its own delta-t and sends TT), a node/apsis record, and a
# centered request -- each against the oracle calling the same ET entry
# point with the same instant. The pctr leg is what caught increment 1
# handing swe_calc_pctr_r a UT instant.
# TO8's additions (EPHEMERIS_REVIEW.md T8): a heliocentric leg -- SEFLG_HELCTR
# through both ends' iflag -- and two topocentric places, Greenwich and
# Sydney, at two bodies: a pooled context keeping a stale swe_set_topo
# from an earlier request answers the second place with the first one's
# horizon, and only differing places catch it. TOPO is the observer the
# oracle sets; the client's --topo carries the same three numbers, and
# --iflag carries SEFLG_TOPOCTR (0x2000000) for both ends.
leg() {   # leg <label> <oracle-mode> <oracle-ipl> <iflagHex> <client args...>
  local label=$1 mode=$2 ipl=$3 iflagHex=$4; shift 4
  "$ROOT/eph_wsclient" --port "$PORT" --jd 2415020.5 --step 600 --count 1 \
    --out "$SCRATCH/leg.txt" --quiet "$@" \
    || { echo "GOLDEN FAIL: $label request failed"; exit 1; }
  while read -r idx who retFlag lon lat dist slon slat sdist; do
    if ! oracle=$("$SCRATCH/oracle" 2415020.5 "$ipl" "$iflagHex" "0" "$EPH" "$mode" ${TOPO:-}); then
      echo "GOLDEN FAIL: $label oracle refused: $oracle"; exit 1
    fi
    got="$lon $lat $dist $slon $slat $sdist"
    TRIED=$((TRIED + 1))
    if [ "$got" != "$oracle" ]; then
      FAIL=$((FAIL + 1))
      echo "$label MISMATCH ($who)"; echo "  server: $got"; echo "  oracle: $oracle"
    fi
    # The flags the server computed with must name the Swiss files: a
    # Moshier fallback answers close enough to look right in a spot check,
    # and nothing compared this column (EPHEMERIS_REVIEW.md T13).
    if [ $((retFlag & 2)) -eq 0 ]; then
      FAIL=$((FAIL + 1))
      echo "$label FLAGS ($who): retFlag $retFlag lacks SEFLG_SWIEPH"
    fi
  done < "$SCRATCH/leg.txt"
  unset TOPO
}
leg "TT" tt 1 0 --tt --objs 1
leg "TT" tt 4 0 --tt --objs 4
for p in 1 2 3 4; do
  leg "NODAPS mean $p" "nodaps:$p:0" 4 0 --tt --nodaps "4,$p,0"
  leg "NODAPS oscu $p" "nodaps:$p:1" 4 0 --tt --nodaps "4,$p,1"
done
leg "PCTR" "pctr:5" 4 0 --tt --objs 4 --center 5
leg "HELIO" ut 4 800000 --objs 4 --iflag 800000
leg "HELIO" ut 1 800000 --objs 1 --iflag 800000
TOPO="0.0 51.5 24" \
  leg "TOPO greenwich" ut 1 2000000 --objs 1 --topo "0.0,51.5,24" --iflag 2000000
TOPO="0.0 51.5 24" \
  leg "TOPO greenwich" ut 4 2000000 --objs 4 --topo "0.0,51.5,24" --iflag 2000000
TOPO="151.2 -33.9 50" \
  leg "TOPO sydney" ut 1 2000000 --objs 1 --topo "151.2,-33.9,50" --iflag 2000000
TOPO="151.2 -33.9 50" \
  leg "TOPO sydney" ut 4 2000000 --objs 4 --topo "151.2,-33.9,50" --iflag 2000000
# And the pre-1955 instant above is the one where a UT-vs-TT confusion
# shows: a UT request for the Moon must NOT equal the TT oracle.
"$ROOT/eph_wsclient" --port "$PORT" --jd 2415020.5 --step 600 --count 1 \
  --objs 1 --out "$SCRATCH/ut.txt" --quiet
read -r idx who retFlag lon lat dist slon slat sdist < "$SCRATCH/ut.txt"
if [ "$lon $lat $dist $slon $slat $sdist" = "$("$SCRATCH/oracle" 2415020.5 1 0 0 "$EPH" tt)" ]; then
  echo "GOLDEN FAIL: a UT request answered as if it were TT"; exit 1
fi
TRIED=$((TRIED + 1))

if [ "$FAIL" -gt 0 ]; then
  echo "GOLDEN FAIL: $FAIL of $TRIED columns mismatched"
  exit 1
fi
echo "GOLDEN PASS: $TRIED columns bit-exact against $SWE_HOME"
