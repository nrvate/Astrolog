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

# -- the advertised bound (A.3 0x0013) against what the server actually does --
#
# 3.5a: a server whose rates differ from a central difference of its own
# positions by more than 1e-5 deg/day or 1e-6 AU/day "states its largest such
# difference" here. THAT IS A PROMISE AND IT WAS WRONG: the number was
# measured geocentrically on five bodies, and a TOPOCENTRIC Moon misses by
# 8e-4 deg/day while the advertisement said 3e-6. A client trusting it was out
# by a factor of 264.
#
# So the grid below sweeps the observers as well as the bodies, and the
# advertised figure has to cover the worst of them.
#
# AND THE PLANETARY MEAN APSIDES, which the first version of this grid left
# out and which are the worst thing in it by an order of magnitude. That
# omission is the same failure the bound itself had: a sweep is only as wide
# as its object list, and bodies plus the Moon's points is not the object
# list a client has. Mercury's MEAN PERIHELION misses by 4.7e-2 deg/day
# geocentrically -- nine times the 5e-3 that was advertised after the first
# sweep -- and heliocentrically it is 1.6e-7. So it is the geocentric
# RE-CENTRING whose own rate is left out, which is registry 2.8's finding
# again in a second place: a transformation applied to the position and not
# to the rate beside it. Found by the Prometheia cross-test's rates leg
# reporting our mean perihelia at 0.387 against their 0.401 deg/day, and
# their number is what differencing Swiss's own longitudes gives.
echo
echo "== the advertised rates bound covers what the server actually does"
./eph_wsclient --host 127.0.0.1 --port "$PORT" --objs 10 --count 1 \
  --out /dev/null > "$SCRATCH/welcome.txt"
# TWO SITES AND FIVE EPOCHS, and both widenings were paid for in findings.
#
# This grid had ONE site (Zurich) and three epochs, and its worst was
# 8.216e-4 deg/day. The other project's sweep, which has two sites and five
# epochs, measured 1.3939e-3 on the SAME object by the SAME method -- the
# topocentric Moon -- at QUITO in 2100. Their four worst rows are all the
# Moon; three of them are at a site or an epoch this grid did not sample.
#
# So the object list had converged and the SAMPLING had not, which is a
# different axis from the previous time this bound was wrong (that was the
# object list: bodies and the Moon's points, no planetary apsides). Quito is
# on the equator at 2,850 m, where the diurnal parallax is largest; 2100 is
# where delta-T's extrapolation is worst.
#
# The lesson is not "add these two" -- it is that a bound is a promise about
# inputs nobody has tried, so the advertisement carries headroom over the
# widest thing measured rather than sitting on it. See BuildWelcome().
for obs in geo topo-zurich topo-quito; do
  # A site is only meaningful for the topocentric observer, and sending one
  # otherwise is ERROR 1 by 3.4 -- which is the server being right.
  case "$obs" in
    topo-zurich) OBSKIND=topo; SITE=",site=8.55:47.37:400" ;;
    topo-quito)  OBSKIND=topo; SITE=",site=-78.5:-0.22:2850" ;;
    *)           OBSKIND=$obs; SITE="" ;;
  esac
  run_obs() {
    ./eph_wsclient --host 127.0.0.1 --port "$PORT" --quiet \
      --jd "$(python3 -c "print(repr($2 - 2.0/1024.0))")" \
      --step-ns 84375000000 --count 5 \
      --profile "obs=$OBSKIND$SITE,plane=ecl,form=sph,speeds=1" \
      --objs 10,301,199,299,499,5,6,7,8,9 \
      --points 301:0:1,301:1:1,301:3:1,4:2:0,5:2:0,199:2:0,4:0:0,199:2:1 \
      --stars Sirius,Polaris,Aldebaran,Vega \
      --out "$3" > /dev/null
  }
  # 1800 is in the grid on purpose: the worst case is there, not at J2000.
  # 1900 and 2100 joined it when the other project's wider sweep found both
  # outside this one.
  for jd in 2378496.5 2415020.5 2451545.0 2461300.5 2488069.5; do
    run_obs "$obs" "$jd" "$SCRATCH/b-$obs-$jd.txt"
  done
done
python3 - "$SCRATCH/welcome.txt" "$SCRATCH"/b-*.txt <<'PY' || fail=1
import sys, re, glob

adv = re.search(r"ratebound=([0-9.eE+-]+)/([0-9.eE+-]+)", open(sys.argv[1]).read())
if not adv:
    print("  WELCOME states no rates bound at all"); sys.exit(1)
advDeg, advAu = float(adv.group(1)), float(adv.group(2))
h = 1.0 / 1024.0
worstA = worstR = 0.0; whoA = whoR = ""
for path in sys.argv[2:]:
    rows = {}
    for line in open(path):
        f = line.split()
        if len(f) < 11 or int(f[2]) != 0:
            continue
        rows.setdefault(f[1], []).append([float.fromhex(x) for x in f[5:11]])
    for label, seq in rows.items():
        if len(seq) != 5:
            continue
        for col in (0, 1, 2):
            v = [r[col] for r in seq]
            if col == 0:
                for i in range(1, 5):
                    while v[i] - v[i-1] > 180.0: v[i] -= 360.0
                    while v[i] - v[i-1] < -180.0: v[i] += 360.0
            d = (v[0] - 8*v[1] + 8*v[3] - v[4]) / (12.0 * h)
            m = abs(seq[2][col+3] - d)
            if col == 2:
                if m > worstR: worstR, whoR = m, "%s %s" % (label, path.split("/")[-1])
            elif m > worstA:
                worstA, whoA = m, "%s %s col%d" % (label, path.split("/")[-1], col)
print("    advertised  %g deg/day   %g AU/day" % (advDeg, advAu))
print("    measured    %g deg/day   %g AU/day" % (worstA, worstR))
print("    worst angle    %s" % whoA)
print("    worst distance %s" % whoR)
bad = worstA > advDeg or worstR > advAu
print("    %s" % ("BAD: the server misses by more than it advertises"
                  if bad else "the advertisement covers it"))
sys.exit(1 if bad else 0)
PY

# -- META's ratesApprox says which objects, not merely that some exist --
#
# 3.5a puts the flag on "the objects concerned". It used to go on EVERY object
# carrying speeds, which is not wrong so much as empty: a client could not use
# it to tell anything from anything. Worse, it was being claimed about the one
# class of rate this server computes as a true derivative of the positions it
# answers -- a MEAN node or apsis, differenced in ephnodrate.h -- so the flag
# said "may be approximate" about the only rates here that provably are not.
#
# TWO ASSERTIONS, and they fail in opposite directions on purpose.
#
#   SOUNDNESS, which is the one that matters to a client: no object whose
#   measured miss exceeds 3.5a's tolerance may have the flag CLEAR. A wrong
#   clear is a lie a client acts on; a wrong set is only noise.
#
#   INFORMATIVENESS: every mean point must have the flag SET CLEAR and must
#   measure 0.000e+00. Without this the flag can rot back to "always on" and
#   the soundness test above would go on passing, because always-on is sound.
#   That is the shape of check this project keeps getting wrong -- a test that
#   passes for the degenerate answer -- so it is spelt out rather than implied.
#
# It reads the SAME files the bound leg above generated, so the grid is the
# one already argued for there (three observers' worth of bodies and points at
# 1800, J2000 and 2026) rather than a second one that could drift from it.
echo
echo "== META ratesApprox names the objects concerned, not all of them"
python3 - "$SCRATCH"/b-*.txt <<'PY' || fail=1
import sys, collections
h = 1.0 / 1024.0
kRatesApprox = 64
worst = {}    # (file,label) -> [ang, dist, flags]
for path in sys.argv[1:]:
    rows, flags = collections.defaultdict(list), {}
    for line in open(path):
        f = line.split()
        if len(f) < 11 or int(f[2]) != 0:
            continue
        rows[f[1]].append([float.fromhex(x) for x in f[5:11]])
        flags[f[1]] = int(f[4])
    for label, seq in rows.items():
        if len(seq) != 5:
            continue
        a = r = 0.0
        for col in (0, 1, 2):
            v = [x[col] for x in seq]
            if col == 0:
                for i in range(1, 5):
                    while v[i] - v[i-1] >  180.0: v[i] -= 360.0
                    while v[i] - v[i-1] < -180.0: v[i] += 360.0
            d = (v[0] - 8*v[1] + 8*v[3] - v[4]) / (12.0 * h)
            m = abs(seq[2][col+3] - d)
            if col == 2: r = max(r, m)
            else:        a = max(a, m)
        worst[(path, label)] = [a, r, flags[label]]

# A mean point is "o:<naif>/<point>/0" -- method 0 is mean (A.5).
def fMean(label):
    return label.startswith("o:") and label.rsplit("/", 1)[-1] == "0"

bad = 0
unsound = [(k, v) for k, v in worst.items()
           if (v[0] > 1e-5 or v[1] > 1e-6) and not (v[2] & kRatesApprox)]
for (path, label), v in sorted(unsound):
    print("    UNSOUND %-12s %9.3e deg/day %9.3e AU/day, flag CLEAR  %s"
          % (label, v[0], v[1], path.split("/")[-1]))
    bad += 1

means = {k: v for k, v in worst.items() if fMean(k[1])}
if not means:
    print("    NO MEAN POINTS IN THE GRID -- this leg would assert nothing")
    bad += 1
for (path, label), v in sorted(means.items()):
    if v[2] & kRatesApprox:
        print("    STILL FLAGGED %-12s %s -- differenced rates are not approximate"
              % (label, path.split("/")[-1]))
        bad += 1
    elif v[0] != 0.0 or v[1] != 0.0:
        print("    NOT EXACT %-12s %9.3e / %9.3e -- flag is cleared on trust"
              % (label, v[0], v[1]))
        bad += 1

nFlag = sum(1 for v in worst.values() if v[2] & kRatesApprox)
print("    %d objects measured, %d flagged, %d mean points exact and unflagged"
      % (len(worst), nFlag, len(means)))
if nFlag == len(worst):
    print("    BAD: every object is flagged, so the flag says nothing")
    bad += 1
print("    %s" % ("BAD" if bad else
      "no object over the tolerance is unflagged, and every mean point is exact"))
sys.exit(1 if bad else 0)
PY

if [ "$fail" -eq 0 ]; then
  echo
  echo "RATES PASS: every row's longitude rate describes its own longitudes,"
  echo "an instant-defined zodiac is no worse than an epoch-anchored one, the"
  echo "advertised rates bound covers the worst the server actually does, and"
  echo "ratesApprox names the objects concerned rather than all of them."
  exit 0
fi
echo
echo "RATES FAIL"
exit 1
