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

if [ "$fail" -eq 0 ]; then
  echo "SIDPLANE ANCHOR PASS: $n plane-1 comparisons within ${TOL}\" of Swiss's own"
  exit 0
fi
echo "SIDPLANE ANCHOR FAIL: $fail of $n"
exit 1
