#!/bin/bash
# tools/sidplane-anchor.sh -- does the in-house sidereal anchor reproduce
# Swiss's own?
#
# Registry 4.1 took A.8's sidereal planes 1 and 2 away from Swiss so that a
# zodiac's zero point could be the direction the zodiac NAMES. Plane 2 changed
# on purpose. PLANE 1 DID NOT, and that is the point of this gate: the anchor
# ecliptic is the plane the zodiac is defined on, so the zero point lies in it
# and the two readings coincide. Any drift on plane 1 is therefore a defect in
# the anchor itself, not a change of meaning.
#
# That is not hypothetical. The first version of 4.1 built the anchor from
# swe_get_ayanamsa_ex at t0, which is the TRUE ayanamsa -- the arc from the
# TRUE equinox -- and placed it on the MEAN ecliptic of t0. The zero point
# moved by the nutation in longitude at t0: -3.311" for Fagan/Bradley,
# +16.777" for Lahiri, +17.346" for Raman. Zodiac-dependent, and so looking
# exactly like the defect 4.1 exists to remove. The other engine's cross-test
# caught it because their plane 1 had agreed with ours to 0.003" and stopped.
# This is that check, kept here so it does not need another engine.
#
# The bodies are 4, 5 and 6 -- Mars, Jupiter, Saturn -- because those are the
# three where the NAIF id the client sends and the Swiss planet number the
# oracle takes are the SAME NUMBER. Using 2 for both asks the server about the
# Venus barycentre and the oracle about Mercury, and "fails" by tens of
# degrees; the first run of this script did exactly that.
#
# It is NOT in tools/ephsrv-golden.sh. The in-house arithmetic agrees with
# Swiss to 0.0000" but not bit for bit -- a different path rounds differently
# in the last places -- and bit-exactness is that gate's whole contract.
#
#   tools/sidplane-anchor.sh            # starts its own server on a scratch port
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 1
PORT=${PORT:-47433}
TOL=${TOL:-0.01}                       # arcsec; the defect it watches was 3-17"
SCRATCH=$(mktemp -d); trap 'rm -rf "$SCRATCH"; [ -n "${SRV:-}" ] && kill "$SRV" 2>/dev/null' EXIT

[ -x ./astrolog-ephd ] && [ -x ./eph_wsclient ] || make -s ephsrv >/dev/null || exit 1

cat > "$SCRATCH/oracle.c" <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include "swephexp.h"
int main(int c, char **v) {
  swe_ctx *x = swe_ctx_new(); char e[AS_MAXCH]; double xx[6];
  (void)c;
  swe_set_ephe_path_r(x, v[4]);
  if (atoi(v[3]) < 0) {
    // ayanamsa mode: apparent and true, so the caller can require the server
    // to match one and differ from the other.
    double app = 0, tru = 0;
    swe_set_sid_mode_r(x, atoi(v[1]), 0, 0);
    if (swe_get_ayanamsa_ex_r(x, atof(v[2]), SEFLG_SWIEPH, &app, e) < 0 ||
        swe_get_ayanamsa_ex_r(x, atof(v[2]),
          SEFLG_SWIEPH | SEFLG_NOABERR | SEFLG_NOGDEFL, &tru, e) < 0) {
      fprintf(stderr, "%s\n", e); return 1;
    }
    printf("%.12f %.12f\n", app, tru);
    swe_ctx_free(x); return 0;
  }
  swe_set_sid_mode_r(x, atoi(v[1]) | SE_SIDBIT_ECL_T0, 0, 0);
  if (swe_calc_r(x, atof(v[2]), atoi(v[3]),
      SEFLG_SWIEPH | SEFLG_SIDEREAL | SEFLG_SPEED, xx, e) < 0) {
    fprintf(stderr, "%s\n", e); return 1;
  }
  printf("%.12f %.12f\n", xx[0], xx[1]);
  swe_ctx_free(x); return 0;
}
EOF
cc -O2 -I/shares/swisseph -o "$SCRATCH/oracle" "$SCRATCH/oracle.c" \
  /shares/swisseph/libswe.a -lm -lpthread 2>/dev/null || {
  echo "sidplane-anchor: skipped (no /shares/swisseph to build the oracle)"; exit 0; }

./astrolog-ephd --bind 127.0.0.1 --port "$PORT" --threads 1 \
  --ephe "$ROOT/ephem;$ROOT" > "$SCRATCH/srv.log" 2>&1 &
SRV=$!
for _ in $(seq 1 100); do
  grep -q "evt=listen port=" "$SCRATCH/srv.log" 2>/dev/null && break
  kill -0 "$SRV" 2>/dev/null || { echo "sidplane-anchor: server did not start"; exit 1; }
  sleep 0.05
done

fail=0; n=0
# Swiss sid mode number, A.11 token. Every t0-anchored zodiac the server serves
# on a fixed plane and Swiss serves through SE_SIDBIT_ECL_T0.
for spec in "0 fagan-bradley" "1 lahiri" "3 raman" "5 krishnamurti" "18 j2000" "20 b1950"; do
  mode=${spec%% *}; tok=${spec##* }
  for jd in 2415020.5 2451545.0 2461000.5; do
    for ipl in 4 5 6; do
      sw=$("$SCRATCH/oracle" "$mode" "$jd" "$ipl" "$ROOT/ephem:$ROOT" 2>/dev/null) || continue
      row=$(./eph_wsclient --host 127.0.0.1 --port "$PORT" --quiet --jd "$jd" --count 1 \
        --out /dev/stdout --profile "zodiac=$tok,sidplane=1,corr=7" --objs "$ipl" 2>/dev/null |
        grep -v '^META' | head -1)
      [ -z "$row" ] && { echo "FAIL $tok jd=$jd ipl=$ipl: no row"; fail=$((fail+1)); continue; }
      n=$((n+1))
      out=$(SW="$sw" ROW="$row" TOL="$TOL" python3 -c '
import os, sys
sw = os.environ["SW"].split(); f = os.environ["ROW"].split()
dl = (float.fromhex(f[5]) - float(sw[0])) * 3600.0
db = (float.fromhex(f[6]) - float(sw[1])) * 3600.0
if dl > 180*3600: dl -= 360*3600
if dl < -180*3600: dl += 360*3600
bad = abs(dl) > float(os.environ["TOL"]) or abs(db) > float(os.environ["TOL"])
print("%.5f %.5f %s" % (dl, db, "BAD" if bad else "ok"))')
      case "$out" in
        *BAD*) echo "FAIL $tok jd=$jd ipl=$ipl: plane 1 vs Swiss $out"; fail=$((fail+1));;
      esac
    done
  done
done

# ---- the twelve zodiacs with NO anchor epoch --------------------------------
# Swiss's table carries t0 = 0 for them: their zero point is defined by where
# something IS -- a star, the galactic centre, the galactic node -- at the
# instant asked, so 3.5a's "A0 on the mean ecliptic of t0" has nothing to carry
# and the object must be refused (errCode 2) on both fixed planes.
#
# THE EPHEMERIS PATH HERE IS LOAD-BEARING. The refusal used to be an accident:
# the code asked for the ayanamsa at JD 0 and that FAILED only because reaching
# 4713 BCE needs seplm48.se1. On a checkout with the bundled ephemeris the
# object was refused and the refusal looked deliberate; with a full mount the
# anchor was built at 4713 BCE and the chart came back with errCode 0 and a
# wrong number. So this leg puts a deep-time ephemeris on the path when the
# machine has one -- it must STILL refuse.
DEEP=""
for d in /swe /shares/swisseph/ephe; do
  [ -f "$d/seplm48.se1" ] && DEEP="$d;" && break
done
[ -n "$DEEP" ] && echo "  (deep-time ephemeris found: ${DEEP%;} -- the refusals below are not a missing file)"
if [ -n "$DEEP" ]; then
  kill "$SRV" 2>/dev/null; sleep 1
  ./astrolog-ephd --bind 127.0.0.1 --port "$PORT" --threads 1 \
    --ephe "$DEEP$ROOT/ephem;$ROOT" > "$SCRATCH/srv2.log" 2>&1 &
  SRV=$!
  for _ in $(seq 1 100); do
    grep -q "evt=listen port=" "$SCRATCH/srv2.log" 2>/dev/null && break
    sleep 0.05
  done
fi
# Plane 1 is refused and plane 2 is ANSWERED, which is the whole of 3.5a's
# instant-defined clause: plane 1 IS the ecliptic of the anchor epoch and these
# have none, while plane 2 takes its zero point from the instant asked -- for
# these zodiacs the definition rather than an approximation of one. Asserting
# both halves matters: a server that refused everything would pass a
# plane-1-only check, and one that answered everything would pass a
# plane-2-only one.
PREV=""
for tok in true-citra true-revati true-pushya true-mula true-sheoran \
           galcent-0sag galcent-cochrane galcent-rgilbrand galcent-mula-wilhelm \
           galequ-iau1958 galequ-true galequ-mula; do
  for sp in 1 2; do
    row=$(./eph_wsclient --host 127.0.0.1 --port "$PORT" --quiet --jd 2451545.0 \
      --count 1 --out /dev/stdout --profile "zodiac=$tok,sidplane=$sp,corr=7" \
      --objs 4 2>/dev/null | grep -v '^META' | head -1)
    n=$((n+1))
    if [ "$sp" = 1 ]; then
      case "$row" in
        *" 2 0 "*) ;;                     # errCode 2, rowsOk 0: refused
        *) echo "FAIL $tok plane 1: expected errCode 2 (no anchor epoch), got: $row"
           fail=$((fail+1));;
      esac
    else
      case "$row" in
        *" 0 1 "*) ;;                     # errCode 0, rowsOk 1: answered
        *) echo "FAIL $tok plane 2: expected an answer, got: $row"
           fail=$((fail+1)); continue;;
      esac
      # And each zodiac's own zero point, not one shared by all of them: a
      # missing swe_set_sid_mode made four different zodiacs return the
      # IDENTICAL plane-2 longitude, which no check on one token could see.
      lon=$(echo "$row" | cut -d' ' -f6)
      if [ "$lon" = "$PREV" ]; then
        echo "FAIL $tok plane 2: same longitude as the previous zodiac ($lon)"
        fail=$((fail+1))
      fi
      PREV=$lon
    fi
  done
done

# ---- 3.5a's anchors sentence: the anchor at its TRUE position --------------
# Swiss builds these twelve zodiacs' ayanamsa from the anchor's APPARENT
# position, which makes the zero point carry the Earth's own motion -- 40.179"
# peak to peak over a year for Spica, and a sidereal zero point that
# oscillates annually is not a fixed reference. 3.5a takes the true position.
#
# Asserted against Swiss's OWN two answers, so it needs no second engine and
# no recorded constant: the server's ayanamsa must equal the NOABERR|NOGDEFL
# one and must DIFFER from the apparent one. The second half matters -- the
# first alone would pass on a server that had never heard of the rule if the
# two happened to coincide for that anchor.
for spec in "27 true-citra" "28 true-revati" "29 true-pushya" "35 true-mula" \
            "17 galcent-0sag" "40 galcent-cochrane" "30 galcent-rgilbrand" \
            "31 galequ-iau1958" "32 galequ-true" "33 galequ-mula"; do
  mode=${spec%% *}; tok=${spec##* }
  for jd in 2415020.5 2451545.0 2461000.5; do
    pair=$("$SCRATCH/oracle" "$mode" "$jd" -1 "$ROOT/ephem:$ROOT" 2>/dev/null) || continue
    row=$(./eph_wsclient --host 127.0.0.1 --port "$PORT" --quiet --jd "$jd" --count 1 \
      --out /dev/stdout --profile "zodiac=$tok,sidplane=0,corr=7,cols=2" --objs 4 \
      2>/dev/null | grep -v '^META' | head -1)
    n=$((n+1))
    out=$(PAIR="$pair" ROW="$row" python3 -c '
import os
app, tru = [float(x) for x in os.environ["PAIR"].split()]
f = os.environ["ROW"].split()
a = float.fromhex(f[11])
dt = abs(a - tru) * 3600.0
da = abs(a - app) * 3600.0
print("%.5f %.5f %s" % (dt, da, "BAD" if dt > 0.01 else ("SAME" if da < 0.01 else "ok")))')
    case "$out" in
      *BAD*) echo "FAIL $tok jd=$jd: ayanamsa is not the TRUE-anchor one ($out)"
             fail=$((fail+1));;
      *SAME*) ;;   # anchor has no aberration at this instant; nothing to tell apart
    esac
  done
done

if [ "$fail" -eq 0 ]; then
  echo "SIDPLANE ANCHOR PASS: $n checks -- plane 1 within ${TOL}\" of Swiss's own, the anchorless zodiacs refused on plane 1 and answered on plane 2"
  exit 0
fi
echo "SIDPLANE ANCHOR FAIL: $fail of $n"
exit 1
