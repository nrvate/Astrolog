#!/bin/bash
# tools/ephsrv-golden.sh -- the ephemeris server's golden gate.
#
# Starts astrolog-ephd on a scratch port, asks it protocol version 4
# questions through eph_wsclient, and compares every column bit-exact
# (hexfloat string equality) against the same Swiss Ephemeris the server
# links: a tiny probe compiled here from $(SWE_HOME)/libswe.a, making the
# call each question should become -- entry point, body, SEFLG bits, sidereal
# mode, site -- with the flags written out HERE, by hand, from Appendix B of
# EPHEMERIS_PLUGINS_PLAN.md, not taken from ephsrv/ephswiss.h. So a server
# that maps a profile to the wrong flags fails here, however consistent it
# is with itself. swetest is not the oracle because its output is decimal
# and rounded; the probe prints raw IEEE bits, so agreement means the
# server's answers ARE the fork's answers, not close to them.
#
# The legs: eleven bodies at six instants across sepl_18 in TT and one in
# UT1; UT1 with the client's own delta T; a grid of rows; tropical and
# sidereal (Fagan-Bradley on the ecliptic of date, on the invariable plane,
# Lahiri on the ecliptic of its epoch); heliocentric, barycentric,
# topocentric at two sites and planet-centred (swe_calc_pctr) observers;
# the equatorial plane, rectangular form, J2000, ICRF and mean-of-date
# frames; the correction masks 0, 1, 3 and 5; no speeds; orbit points by
# swe_nod_aps and by the Moon's named node and apogee bodies, the
# descending node as the opposite point; a hypothetical; a designation; and
# the ayanamsa and delta T columns.
#
# Knobs (env):
#   SWE_HOME   where the thread-safe fork lives  (default /shares/swisseph)
#   EPH        the ephemeris dir handed to --ephe (default: the repo's)
#   PORT       scratch port                      (default 28000 + pid % 400)
#   TLS        1 to run the whole comparison over wss:// against a throwaway
#              CA and a localhost certificate. Same columns, same bits.
#   PROTO      the highest version the client offers (default 4). PROTO=5 is
#              a newer client, which must be welcomed in 4 and answered the
#              same bits.
#
# Exit 0 with "GOLDEN PASS" when every column matches; nonzero with the
# first mismatches otherwise. Cleans up its server and scratch files.

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

[ -x "$ROOT/astrolog-ephd" ] && [ -x "$ROOT/eph_wsclient" ] || make -s ephsrv >/dev/null

# The oracle probe: one Swiss call, printed as the wire's columns.
cat > "$SCRATCH/oracle.c" << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "swephexp.h"
int main(int argc, char **argv) {
  /* argv: jd ipl iflagHex sidMode ephePath mode [lon lat alt]
     mode: tt       swe_calc_r at jd (TT)
           ut       swe_calc_ut_r at jd (UT1)
           nodaps:P:M  swe_nod_aps_r at jd, answer P (0 ascending, 1
                    descending, 2 perihelion, 3 aphelion), method M
                    (SE_NODBIT_* value)
           opposite swe_calc_r, then the opposite point: lon + 180 and
                    latitude and its speed negated
           pctr:C   swe_calc_pctr_r at jd centred on body C
           aya      swe_calc_r, then one more column: the ayanamsa of the
                    mode (swe_get_ayanamsa_ex_r at jd with the same flags)
     The three trailing arguments, taken when iflag carries SEFLG_TOPOCTR,
     are the site. The ephemeris bit (SEFLG_SWIEPH) is always added; no
     other flag is -- the gate writes each one. */
  double jd = atof(argv[1]);
  int ipl = atoi(argv[2]);
  int32 iflag = (int32)strtoul(argv[3], NULL, 16) | SEFLG_SWIEPH;
  /* sidMode is "mode" or "mode:t0:ayanamsa" -- the user zodiac's anchor. */
  int sidMode = atoi(argv[4]);
  double sidT0 = 0, sidAyan = 0;
  sscanf(argv[4], "%*d:%lf:%lf", &sidT0, &sidAyan);
  const char *mode = argv[6];
  char serr[256];
  double xx[6], daya = 0;
  int32 ret, fAya = 0;
  /* The context API, exactly the server's path: the process-global config
     (ephe path) published, then a fresh context that inherits it. */
  swe_set_ephe_path(argv[5]);
  swe_ctx *ctx = swe_ctx_new();
  if (iflag & SEFLG_SIDEREAL)
    swe_set_sid_mode_r(ctx, sidMode, sidT0, sidAyan);
  if (iflag & SEFLG_TOPOCTR) {
    if (argc < 10) { printf("ERR topo needs lon lat alt\n"); return 1; }
    swe_set_topo_r(ctx, atof(argv[7]), atof(argv[8]), atof(argv[9]));
  }
  if (strncmp(mode, "nodaps:", 7) == 0) {
    int pnt = 0, meth = 0;
    double xn[6], xd[6], xp[6], xa[6], *px;
    sscanf(mode + 7, "%d:%d", &pnt, &meth);
    ret = swe_nod_aps_r(ctx, jd, ipl, iflag, meth, xn, xd, xp, xa, serr);
    px = pnt == 0 ? xn : pnt == 1 ? xd : pnt == 2 ? xp : xa;
    for (int i = 0; i < 6; i++) xx[i] = px[i];
  } else if (strncmp(mode, "pctr:", 5) == 0) {
    ret = swe_calc_pctr_r(ctx, jd, ipl, atoi(mode + 5), iflag, xx, serr);
  } else if (strcmp(mode, "ut") == 0) {
    ret = swe_calc_ut_r(ctx, jd, ipl, iflag, xx, serr);
  } else {
    ret = swe_calc_r(ctx, jd, ipl, iflag, xx, serr);
    if (ret >= 0 && strcmp(mode, "opposite") == 0) {
      xx[0] = swe_degnorm(xx[0] + 180.0);
      xx[1] = -xx[1];
      xx[4] = -xx[4];
    }
    if (ret >= 0 && strcmp(mode, "aya") == 0) {
      fAya = 1;
      if (swe_get_ayanamsa_ex_r(ctx, jd, iflag, &daya, serr) < 0) ret = -1;
    }
  }
  if (ret < 0) { printf("ERR %s\n", serr); return 1; }
  printf("%a %a %a %a %a %a", xx[0], xx[1], xx[2], xx[3], xx[4], xx[5]);
  if (fAya) printf(" %a", daya);
  printf("\n");
  return 0;
}
EOF
# Static archive, not -lswe: an installed libswe.so (this machine keeps a
# stale one in /usr/local/lib) wins the runtime search over -L, and the
# oracle would then answer from a different library than the server.
gcc -O2 -I"$SWE_HOME" "$SCRATCH/oracle.c" "$SWE_HOME/libswe.a" -lm -ldl \
  -lpthread -o "$SCRATCH/oracle"

TLS_SRV=() CLI=()
if [ "${TLS:-0}" = 1 ]; then
  (cd "$SCRATCH" &&
   openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes \
     -keyout ca.key -out ca.pem -days 2 -subj /CN=ephsrv-golden-ca &&
   openssl req -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes \
     -keyout srv.key -out srv.csr -subj /CN=localhost &&
   printf 'subjectAltName=DNS:localhost,IP:127.0.0.1\n' > ext.cnf &&
   openssl x509 -req -in srv.csr -CA ca.pem -CAkey ca.key -CAcreateserial \
     -out srv.pem -days 1 -extfile ext.cnf) > "$SCRATCH/openssl.log" 2>&1 ||
    { echo "GOLDEN FAIL: could not make the test certificates"; cat "$SCRATCH/openssl.log"; exit 1; }
  TLS_SRV=(--tls-cert "$SCRATCH/srv.pem" --tls-key "$SCRATCH/srv.key")
  CLI=(--tls --ca "$SCRATCH/ca.pem")
fi
[ -n "${PROTO:-}" ] && CLI+=(--proto "$PROTO")

"$ROOT/astrolog-ephd" --port "$PORT" --ephe "$EPH" --threads 1 --cells-per-sec 0 "${TLS_SRV[@]}" \
  > "$SCRATCH/ephd.log" 2>&1 &
EPHD_PID=$!
for i in $(seq 1 50); do
  grep -q "evt=listen port=" "$SCRATCH/ephd.log" 2>/dev/null && break
  sleep 0.1
done
grep -q "evt=listen port=" "$SCRATCH/ephd.log" || { echo "GOLDEN FAIL: server did not start"; exit 1; }
if [ "${PROTO:-4}" != 4 ]; then
  "$ROOT/eph_wsclient" "${CLI[@]}" --port "$PORT" --objs 10 --count 1 > "$SCRATCH/welcome.txt"
  grep -q '^WELCOME v4 ' "$SCRATCH/welcome.txt" ||
    { echo "GOLDEN FAIL: a protocol ${PROTO} client was not welcomed in 4: $(cat "$SCRATCH/welcome.txt")"; exit 1; }
fi

FAIL=0
TRIED=0
# compare <label> <jd-for-oracle> <ipl> <flagsHex> <sidMode> <mode> <got-file> [row]
# Reads the row-th line (default 1) of the client's --out file: idx label
# errCode rowsOk flags col... and compares its columns with the oracle's.
compare() {
  local label=$1 jd=$2 ipl=$3 fl=$4 sid=$5 mode=$6 file=$7 row=${8:-1} line oracle got
  line=$(sed -n "${row}p" "$file")
  # shellcheck disable=SC2086
  if ! oracle=$("$SCRATCH/oracle" "$jd" "$ipl" "$fl" "$sid" "$EPH" "$mode" ${TOPO:-}); then
    echo "GOLDEN FAIL: $label: the oracle refused: $oracle"; exit 1
  fi
  local n; n=$(echo "$oracle" | wc -w)
  got=$(echo "$line" | awk -v n="$n" '{ s = ""; for (i = 6; i < 6 + n; i++) s = s (i > 6 ? " " : "") $i; print s }')
  TRIED=$((TRIED + 1))
  if [ "$(echo "$line" | awk '{print $3}')" != 0 ] || [ "$got" != "$oracle" ]; then
    FAIL=$((FAIL + 1))
    if [ "$FAIL" -le 6 ]; then
      echo "MISMATCH $label"
      echo "  server: $line"
      echo "  oracle: $oracle"
    fi
  fi
}
# leg <label> <jd> <ipl> <flagsHex> <sidMode> <mode> -- <client args...>
leg() {
  local label=$1 jd=$2 ipl=$3 fl=$4 sid=$5 mode=$6; shift 7
  "$ROOT/eph_wsclient" "${CLI[@]}" --port "$PORT" --count 1 --out "$SCRATCH/leg.txt" --quiet "$@" \
    || { echo "GOLDEN FAIL: $label: the request failed"; exit 1; }
  compare "$label" "$jd" "$ipl" "$fl" "$sid" "$mode" "$SCRATCH/leg.txt"
}

# 1. Eleven bodies, TT, six instants inside sepl_18's 1800-2399 coverage
#    (2378490.5 sits just before the file's start and both ends refuse it).
#    NAIF ids: the Sun, the Moon, Mercury and Venus, the barycentres 4-9,
#    and asteroid 5 (se00005.se1, the SE_AST_OFFSET route).
NAIFS=(10 301 199 299 4 5 6 7 8 9 20000005)
IPLS=(0 1 2 3 4 5 6 7 8 9 10005)
for jd in 2415020.5 2433895.5 2451545.0 2465000.5 2378500.5 2489500.5; do
  "$ROOT/eph_wsclient" "${CLI[@]}" --port "$PORT" --objs "$(IFS=,; echo "${NAIFS[*]}")" --jd "$jd" \
    --count 1 --out "$SCRATCH/got.txt" --quiet \
    || { echo "GOLDEN FAIL: request for JD $jd failed"; exit 1; }
  [ "$(wc -l < "$SCRATCH/got.txt")" -eq 11 ] || { echo "GOLDEN FAIL: JD $jd returned $(wc -l < "$SCRATCH/got.txt") of 11 rows"; exit 1; }
  for k in "${!NAIFS[@]}"; do
    compare "TT jd=$jd naif=${NAIFS[$k]}" "$jd" "${IPLS[$k]}" 100 0 tt "$SCRATCH/got.txt" $((k + 1))
  done
done

# 2. Time. UT1 through swe_calc_ut_r; UT1 with the client's delta T is
#    swe_calc_r at jd + dT; a grid's rows are jd1 + (jd2 + r x step).
leg "UT1 Moon" 2415020.5 1 100 0 ut -- --objs 301 --jd 2415020.5 --ut
leg "UT1 Mars" 2415020.5 4 100 0 ut -- --objs 4 --jd 2415020.5 --ut
DT_JD=$(python3 -c "print(repr(2451545.0 + 64.184 / 86400.0))")
leg "UT1 + deltaTSec Moon" "$DT_JD" 1 100 0 tt -- --objs 301 --jd 2451545.0 --ut --deltat 64.184
"$ROOT/eph_wsclient" "${CLI[@]}" --port "$PORT" --objs 301 --jd 2451545.0 --jd2 0.25 --step 86400 \
  --count 3 --out "$SCRATCH/grid.txt" --quiet || { echo "GOLDEN FAIL: grid request failed"; exit 1; }
for r in 0 1 2; do
  compare "grid row $r" "$(python3 -c "print(repr(2451545.0 + (0.25 + $r)))")" 1 100 0 tt "$SCRATCH/grid.txt" $((r + 1))
done
# The pre-1955 instant is where a UT-vs-TT confusion shows: a UT1 request
# for the Moon must NOT equal the TT oracle.
"$ROOT/eph_wsclient" "${CLI[@]}" --port "$PORT" --jd 2415020.5 --count 1 --objs 301 --ut \
  --out "$SCRATCH/ut.txt" --quiet
if [ "$(cut -d' ' -f6-11 "$SCRATCH/ut.txt")" = "$("$SCRATCH/oracle" 2415020.5 1 100 0 "$EPH" tt)" ]; then
  echo "GOLDEN FAIL: a UT1 request answered as if it were TT"; exit 1
fi
TRIED=$((TRIED + 1))

# 3. Zodiacs. Fagan-Bradley on the ecliptic of date (mode 0), on the
#    invariable plane (SE_SIDBIT_SSY_PLANE, 512), Lahiri on the ecliptic of
#    its epoch (1 + SE_SIDBIT_ECL_T0 = 257), each SEFLG_SIDEREAL (0x10000).
for jd in 2451545.0 2400000.5; do
  leg "sidereal FB Sun jd=$jd" "$jd" 0 10100 0 tt -- --profile zodiac=fagan-bradley --objs 10 --jd "$jd"
  leg "sidereal FB Moon jd=$jd" "$jd" 1 10100 0 tt -- --profile zodiac=fagan-bradley --objs 301 --jd "$jd"
done
leg "sidereal FB invariable plane" 2451545.0 4 10100 512 tt -- --profile zodiac=fagan-bradley,sidplane=2 --objs 4
leg "sidereal Lahiri anchor-epoch plane" 2451545.0 5 10100 257 tt -- --profile zodiac=lahiri,sidplane=1 --objs 5
leg "sidereal FB with the ayanamsa column" 2451545.0 1 10100 0 aya -- --profile zodiac=fagan-bradley,cols=2 --objs 301

# 4. Observers. Heliocentric 0x8, barycentric 0x4000, topocentric 0x8000 at
#    two sites -- a context that kept an earlier site answers the second
#    with the first one's horizon, and only differing sites catch it -- and
#    the Sun seen from Jupiter through swe_calc_pctr.
# Heliocentric and barycentric observers. Swiss turns aberration and
# deflection off inside such a call for a BODY whatever mask was asked
# (plaus_iflag), so the full mask and the light-time mask answer the same
# bits here -- both are asked, and both are compared against a probe that
# passes SEFLG_HELCTR alone.
# A heliocentric BODY may ask only mask 0 or 1 since the per-kind drop:
# Swiss turns aberration and deflection off inside the call for those
# observers, so the masks are not merely unhelpful there, they are not
# advertised. The NUMBERS are unchanged -- corr=1 and corr=7 always gave
# the same answer -- which is the whole reason the wider mask was a lie.
# The nod_aps legs below keep the full mask: an orbit point from the same
# observer genuinely honours it, and 0x0014 is what now says so.
leg "helio Mars" 2415020.5 4 108 0 tt -- --profile obs=helio,corr=1 --objs 4 --jd 2415020.5
leg "helio Mars, light time only" 2415020.5 4 108 0 tt -- --profile obs=helio,corr=1 --objs 4 --jd 2415020.5
leg "helio Moon" 2415020.5 1 108 0 tt -- --profile obs=helio,corr=1 --objs 301 --jd 2415020.5
leg "bary Sun" 2451545.0 0 4100 0 tt -- --profile obs=bary,corr=1 --objs 10
TOPO="0.0 51.5 24" leg "topo greenwich Moon" 2415020.5 1 8100 0 tt -- --profile obs=topo,site=0.0:51.5:24 --objs 301 --jd 2415020.5
TOPO="0.0 51.5 24" leg "topo greenwich Mars" 2415020.5 4 8100 0 tt -- --profile obs=topo,site=0.0:51.5:24 --objs 4 --jd 2415020.5
TOPO="151.2 -33.9 50" leg "topo sydney Moon" 2415020.5 1 8100 0 tt -- --profile obs=topo,site=151.2:-33.9:50 --objs 301 --jd 2415020.5
TOPO="151.2 -33.9 50" leg "topo sydney sidereal invariable" 2415020.5 4 18100 512 tt -- --profile obs=topo,site=151.2:-33.9:50,zodiac=fagan-bradley,sidplane=2 --objs 4 --jd 2415020.5
leg "pctr Sun from Jupiter" 2451545.0 0 700 0 pctr:5 -- --profile obs=body:5,corr=1 --objs 10
# Mask 5, not 7. A planet-centred observer no longer advertises
# deflection: swe_calc_pctr() re-bases aberration on the centring body but
# cannot re-base deflection, since swi_deflect_light() takes no observer and
# uses the Earth's geometry -- refereed by the Prometheia project against
# USNO Circular 179 at up to 0.544", and bending the Sun's own light, which
# is what this very leg used to ask for.
leg "pctr Sun from Jupiter, mask 5" 2451545.0 0 300 0 pctr:5 -- --profile obs=body:5,corr=5 --objs 10
leg "pctr Mars from Jupiter, light time only" 2451545.0 4 700 0 pctr:5 -- --profile obs=body:5,corr=1 --objs 4

# 5. Planes, forms, frames, corrections, speeds.
leg "equatorial Moon" 2451545.0 1 900 0 tt -- --profile plane=equ --objs 301
leg "rectangular Mars" 2451545.0 4 1100 0 tt -- --profile form=rect --objs 4
# 3.5a: a sidereal rectangular answer is the tropical vector rotated about
# the ecliptic pole by the ayanamsa, which Swiss does for SEFLG_XYZ too.
leg "rectangular sidereal Mars" 2451545.0 4 11100 0 tt -- --profile form=rect,zodiac=fagan-bradley --objs 4
leg "J2000 equatorial Sun" 2451545.0 0 960 0 tt -- --profile frame=j2000,plane=equ --objs 10
leg "ICRF Venus" 2451545.0 3 20160 0 tt -- --profile frame=icrf --objs 299
leg "mean of date Mercury" 2451545.0 2 140 0 tt -- --profile frame=mod --objs 199
leg "true positions (mask 0) Mars" 2451545.0 4 110 0 tt -- --profile corr=0 --objs 4
leg "no aberration (mask 3) Mars" 2451545.0 4 500 0 tt -- --profile corr=3 --objs 4
leg "no deflection (mask 5) Mars" 2451545.0 4 300 0 tt -- --profile corr=5 --objs 4
leg "no speeds Moon" 2451545.0 1 0 0 tt -- --profile speeds=0 --objs 301

# 6. Orbit points. swe_nod_aps on Jupiter, all four points, mean
#    (SE_NODBIT_MEAN 1) and osculating (2); the Moon's named bodies --
#    SE_TRUE_NODE 11, SE_INTP_APOG 21, SE_MEAN_APOG 12 -- and the mean
#    descending node as SE_MEAN_NODE 10's opposite point.
for p in 0 1 2 3; do
  leg "nod_aps Jupiter mean point $p" 2415020.5 5 100 0 "nodaps:$p:1" -- --points "5:$p:0" --jd 2415020.5
  leg "nod_aps Jupiter osculating point $p" 2415020.5 5 100 0 "nodaps:$p:2" -- --points "5:$p:1" --jd 2415020.5
done
# swe_nod_aps reads the correction bits itself, before the normalisation
# above, so a heliocentric orbit point answers differently under the full
# mask and under light time alone -- each against its own oracle flags.
leg "helio nod_aps Mars perihelion" 2451545.0 4 108 0 "nodaps:2:1" -- --profile obs=helio --points 4:2:0
leg "helio nod_aps Mars perihelion, light time only" 2451545.0 4 708 0 "nodaps:2:1" -- --profile obs=helio,corr=1 --points 4:2:0
leg "Moon true node" 2451545.0 11 100 0 tt -- --points 301:0:1
leg "Moon interpolated apogee" 2451545.0 21 100 0 tt -- --points 301:3:2
leg "Moon mean apogee, sidereal" 2451545.0 12 10100 0 tt -- --profile zodiac=fagan-bradley --points 301:3:0
leg "Moon mean descending node" 2451545.0 10 100 0 opposite -- --points 301:1:0

# 7. A hypothetical and a designation.
leg "hypothetical cupido" 2451545.0 40 100 0 tt -- --hypo cupido
leg "designation 2060" 2451545.0 15 100 0 tt -- --desig 2060

# 8b. The client's own delta T as a TABLE (A.4 0x8004): a UT1 grid whose
#     rows are converted with the table, interpolated at each row's instant.
#     60 s at the first row, 64 at the third, so the second is 62.
for r in 0 1 2; do
  DT=$(python3 -c "print(repr(2451545.0 + $r + (60.0 + 2.0 * $r) / 86400.0))")
  "$ROOT/eph_wsclient" "${CLI[@]}" --port "$PORT" --objs 301 --ut --jd 2451545 --count 3 \
    --step 86400 --deltat-table 2451545:60,2451547:64 --out "$SCRATCH/dtt.txt" --quiet \
    || { echo "GOLDEN FAIL: the delta T table request failed"; exit 1; }
  compare "delta T table row $r" "$DT" 1 100 0 tt "$SCRATCH/dtt.txt" $((r + 1))
done

# 8c. An instant LIST (3.4), in any order, duplicates allowed.
"$ROOT/eph_wsclient" "${CLI[@]}" --port "$PORT" --objs 4 \
  --list 2451545.0,2415020.5,2451545.0 --out "$SCRATCH/list.txt" --quiet \
  || { echo "GOLDEN FAIL: the instant list request failed"; exit 1; }
compare "instant list row 0" 2451545.0 4 100 0 tt "$SCRATCH/list.txt" 1
compare "instant list row 1" 2415020.5 4 100 0 tt "$SCRATCH/list.txt" 2
compare "instant list row 2" 2451545.0 4 100 0 tt "$SCRATCH/list.txt" 3

# 8d. The focal point (orbit method 4): the empty focus in place of the
#     aphelion, SE_NODBIT_OSCU | SE_NODBIT_FOPOINT = 258.
leg "focal point of Jupiter" 2451545.0 5 100 0 "nodaps:3:258" -- --points 5:3:4

# 8e. Every hypothetical token Swiss carries elements for, against
#     SE_FICT_OFFSET + its index (A.15's order is seorbel.txt's). The last
#     four -- vulcan, white-moon, proserpina, waldemath -- are past Swiss's
#     built-in SE_NFICT_ELEM and need a seorbel.txt on the server's path;
#     without one they are a per-object error, which the leg after this
#     asks for by name rather than letting them pass as silence.
i=0
for tok in cupido hades zeus kronos apollon admetos vulcanus poseidon \
    isis-transpluto nibiru harrington neptune-leverrier neptune-adams \
    pluto-lowell pluto-pickering; do
  leg "hypothetical $tok" 2451545.0 $((40 + i)) 100 0 tt -- --hypo "$tok"
  i=$((i + 1))
done
for tok in vulcan white-moon proserpina waldemath; do
  "$ROOT/eph_wsclient" "${CLI[@]}" --port "$PORT" --count 1 --hypo "$tok" --meta \
    --quiet > "$SCRATCH/hypo.txt" || { echo "GOLDEN FAIL: the $tok request failed"; exit 1; }
  TRIED=$((TRIED + 1))
  if [ -f "$EPH/seorbel.txt" ]; then
    grep -q "err=0 rowsOk=1" "$SCRATCH/hypo.txt" ||
      { FAIL=$((FAIL + 1)); echo "MISMATCH hypothetical $tok: $(cat "$SCRATCH/hypo.txt")"; }
  else
    grep -qv "err=0 " "$SCRATCH/hypo.txt" && grep -q 'rowsOk=0' "$SCRATCH/hypo.txt" ||
      { FAIL=$((FAIL + 1)); echo "MISMATCH hypothetical $tok without seorbel.txt: expected a "
        echo "  per-object error, got: $(cat "$SCRATCH/hypo.txt")"; }
  fi
done

# 8f. A zodiac token from each part of the registry, against its SE_SIDM_*
#     number -- the index in A.11 IS the Swiss mode.
leg "zodiac lahiri" 2451545.0 4 10100 1 tt -- --profile zodiac=lahiri --objs 4
leg "zodiac krishnamurti" 2451545.0 4 10100 5 tt -- --profile zodiac=krishnamurti --objs 4
leg "zodiac true-citra" 2451545.0 4 10100 27 tt -- --profile zodiac=true-citra --objs 4
leg "zodiac lahiri-icrc" 2451545.0 4 10100 46 tt -- --profile zodiac=lahiri-icrc --objs 4
leg "zodiac user" 2451545.0 4 10100 255:2451545:23.85 tt -- --profile zodiac=user,anchor=2451545:23.85 --objs 4

# 8. The delta T column: the client's own, as sent (64.184 s).
"$ROOT/eph_wsclient" "${CLI[@]}" --port "$PORT" --profile cols=8 --objs 10 --count 1 --ut \
  --deltat 64.184 --out "$SCRATCH/dt.txt" --quiet || { echo "GOLDEN FAIL: delta T column request failed"; exit 1; }
TRIED=$((TRIED + 1))
python3 -c "import sys; sys.exit(float.fromhex(sys.argv[1]) != 64.184)" "$(awk '{print $12}' "$SCRATCH/dt.txt")" ||
  { FAIL=$((FAIL + 1)); echo "MISMATCH delta T column: $(cat "$SCRATCH/dt.txt")"; }

if [ "$FAIL" -gt 0 ]; then
  echo "GOLDEN FAIL: $FAIL of $TRIED comparisons mismatched"
  exit 1
fi
echo "GOLDEN PASS: $TRIED comparisons bit-exact against $SWE_HOME$([ "${TLS:-0}" = 1 ] && echo ", over wss://")$([ -n "${PROTO:-}" ] && echo ", protocol $PROTO client")"
