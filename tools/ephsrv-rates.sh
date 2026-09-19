#!/bin/bash
# tools/ephsrv-rates.sh -- a row's reported RATES agree with its own positions.
#
#   tools/ephsrv-rates.sh
#
# Every other gate here grades POSITIONS. The six columns a row carries are
# three positions and three rates, and until this existed nothing had ever
# asked whether the second three described the first three. The idea and the
# method are Ephemeris Prometheia's, from their cross-test's "rates" leg; this
# is the same question asked on this side so the answer does not depend on
# having their harness.
#
# THE ORACLE IS THE SERVER'S OWN POSITIONS. One request of five rows at
# t-2h .. t+2h, and the reported rate at t is compared against a five-point
# central difference of the longitudes in the same answer. Nothing outside is
# consulted, so there is nothing to agree or disagree with: a row either
# describes itself or it does not.
#
# h = 1/1024 day, exactly representable in binary. That matters and is their
# finding: at h = 0.001 day the quantization of the Julian day alone put 7e-7
# deg/day on the Moon, which is the size of the thing being measured.
#
# WHAT IT GRADES, and why it is a comparison rather than an absolute band.
# Swiss's own speeds are not perfect -- geocentric apparent longitude rates sit
# at 1e-6 to 1e-5 deg/day against a difference of its own positions, and a
# topocentric Moon is worse -- and those are Swiss's numbers, entries for the
# registry rather than defects of this server. What IS this server's is any
# rate error that an epoch-anchored zodiac does not have and an instant-defined
# one does: the zodiac's zero point is the only thing that changed.
#
# So each object is asked under BOTH lahiri (anchored to an epoch) and
# true-citra (anchored to where Spica is at the instant), and the second must
# be no worse than the first. The defect this was written for -- the true-anchor
# correction of registry 2.5 applied to the longitude and not to its rate --
# shows up here as 1e-4 against 1e-6, a hundredfold, uniform across every body
# and varying only with the date.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
PORT=${PORT:-$((28800 + $$ % 400))}
EPH=${EPH:-"$ROOT/ephem;$ROOT"}
SCRATCH=$(mktemp -d)
trap 'rm -rf "$SCRATCH"; [ -n "${SRV:-}" ] && kill "$SRV" 2>/dev/null || true' EXIT

make -s ephsrv
./astrolog-ephd --bind 127.0.0.1 --port "$PORT" --threads 2 --ephe "$EPH" \
  > "$SCRATCH/ephd.log" 2>&1 &
SRV=$!
for i in $(seq 1 40); do
  grep -q "evt=listen port=" "$SCRATCH/ephd.log" && break
  sleep 0.25
done
grep -q "evt=listen port=" "$SCRATCH/ephd.log" || {
  echo "RATES FAIL: server did not start"; cat "$SCRATCH/ephd.log"; exit 1; }

# 2 * (1/1024) day before the centre, five rows, one step of 1/1024 day.
run() {   # run <zodiac> <jd-centre> <outfile>
  local jd
  jd=$(python3 -c "print(repr($2 - 2.0/1024.0))")
  ./eph_wsclient --host 127.0.0.1 --port "$PORT" --quiet \
    --jd "$jd" --step-ns 84375000000 --count 5 \
    --profile "plane=ecl,form=sph,speeds=1,zodiac=$1" \
    --objs 10,301,199,499,5,9 --stars Sirius,Aldebaran \
    --out "$3" > /dev/null
}

fail=0
for jd in 2451545.0 2461300.5; do
  run lahiri     "$jd" "$SCRATCH/lahiri.txt"
  run true-citra "$jd" "$SCRATCH/citra.txt"
  python3 - "$SCRATCH/lahiri.txt" "$SCRATCH/citra.txt" "$jd" <<'PY' || fail=1
import sys

def read(path):
    """label -> [(lon, dlon)] in row order, from the client's hexfloat dump."""
    rows = {}
    for line in open(path):
        f = line.split()
        if len(f) < 10:
            continue
        label, err = f[1], int(f[2])
        if err != 0:
            continue
        rows.setdefault(label, []).append(
            (float.fromhex(f[5]), float.fromhex(f[8])))
    return rows

def miss(seq):
    """|reported rate at the centre - five-point central difference of it|."""
    h = 1.0 / 1024.0
    lon = [x for x, _ in seq]
    # The zodiac's own wrap: a body a hair either side of zero would otherwise
    # difference to a full turn.
    for i in range(1, len(lon)):
        while lon[i] - lon[i-1] > 180.0: lon[i] -= 360.0
        while lon[i] - lon[i-1] < -180.0: lon[i] += 360.0
    d = (lon[0] - 8.0*lon[1] + 8.0*lon[3] - lon[4]) / (12.0 * h)
    return abs(seq[2][1] - d)

lah, cit = read(sys.argv[1]), read(sys.argv[2])
jd, bad = sys.argv[3], 0
print("  jd %s" % jd)
print("    %-10s %14s %14s" % ("object", "lahiri", "true-citra"))
for label in sorted(lah):
    if label not in cit or len(lah[label]) != 5 or len(cit[label]) != 5:
        print("    %-10s  MISSING ROWS" % label); bad += 1; continue
    a, b = miss(lah[label]), miss(cit[label])
    # An instant-defined zodiac may be no worse than an epoch-anchored one.
    # The 2e-6 is slack for the two zodiacs' own rounding, not a tolerance on
    # the defect: that one is a hundred times this.
    over = b > a + 2.0e-6
    bad += over
    print("    %-10s %14.3e %14.3e%s" % (label, a, b, "  <-- BAD" if over else ""))
sys.exit(1 if bad else 0)
PY
done

if [ "$fail" -eq 0 ]; then
  echo
  echo "RATES PASS: every row's longitude rate describes its own longitudes,"
  echo "and an instant-defined zodiac is no worse than an epoch-anchored one."
  exit 0
fi
echo
echo "RATES FAIL"
exit 1
