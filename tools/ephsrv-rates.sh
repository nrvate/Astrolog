#!/bin/bash
# tools/ephsrv-rates.sh -- a row's reported RATES agree with its own positions.
#
#   tools/ephsrv-rates.sh              # the gate: needs a server
#   tools/ephsrv-rates.sh --selftest   # the GATE's own decisions, ~1s, no
#                                      # server: 3 controls + 8 injections,
#                                      # each required to red its own leg
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
SELFTEST=0
[ "${1:-}" = "--selftest" ] && SELFTEST=1 || true
PORT=${PORT:-$((28800 + $$ % 400))}
EPH=${EPH:-"$ROOT/ephem;$ROOT"}
SCRATCH=$(mktemp -d)
trap 'rm -rf "$SCRATCH"; [ -n "${SRV:-}" ] && kill "$SRV" 2>/dev/null || true' EXIT

if [ "$SELFTEST" = 0 ]; then
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
fi

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

# The three legs, each ONE copy: the run below calls them and so does
# --selftest, so what the selftest falsifies is what the gate runs.
leg_zodiac() {
  python3 - "$1" "$2" "$3" <<'PY'
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
jd, bad, graded = sys.argv[3], 0, 0
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
    graded += 1
    print("    %-10s %14.3e %14.3e%s" % (label, a, b, "  <-- BAD" if over else ""))

# HOW MANY OBJECTS WERE GRADED, not just whether the graded ones agreed.
#
# Every row above is filtered on err == 0, so a server that failed EVERY
# object leaves lah empty, the loop runs zero times, bad stays 0 and this
# leg exits 0 -- printing its table header and nothing under it. Measured
# 2026-09-20: with all-error rows, and again with two empty files, this leg
# passed. "The instant-defined zodiac is no worse than the epoch-anchored
# one" is trivially true of two zodiacs that answered nothing.
#
# kObjs is the object list run() asks for -- 6 bodies + 2 stars -- declared
# here rather than counted from the request, so the two cannot narrow
# together: if the request shrinks, this fails instead of agreeing with it.
kObjs = 8
if graded != kObjs:
    print("    ONLY %d OF %d OBJECTS GRADED -- the rest errored or never"
          " arrived, and a comparison needs two answers" % (graded, kObjs))
    bad += 1
sys.exit(1 if bad else 0)
PY
}

leg_bound() {
  python3 - "$@" <<'PY'
import sys, re, glob

adv = re.search(r"ratebound=([0-9.eE+-]+)/([0-9.eE+-]+)", open(sys.argv[1]).read())
if not adv:
    print("  WELCOME states no rates bound at all"); sys.exit(1)
advDeg, advAu = float(adv.group(1)), float(adv.group(2))
h = 1.0 / 1024.0
worstA = worstR = 0.0; whoA = whoR = ""; nSeries = 0
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
        nSeries += 1
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
overBound = worstA > advDeg or worstR > advAu
bad = overBound

# A BOUND IS A CLAIM ABOUT WHAT WAS MEASURED, so what was measured has to be
# counted. Every row here is filtered on err == 0 and on having all five
# instants, so a server that failed everything leaves `worst*` at their
# initial 0.0 and this reports "measured 0 deg/day ... the advertisement
# covers it" -- a perfect score for no data. Measured 2026-09-20 by handing
# this leg a file of all-error rows: it passed, and printed an EMPTY "worst
# angle" line while doing it. The empty name was visible and asserted on by
# nothing, which is the same shape as a duplicate scan printing what a clean
# table prints.
#
# 22 objects per file: 10 bodies + 8 orbit points + 4 stars, as run_obs asks
# for them. 15 files: 3 observers x 5 epochs. Both declared here rather than
# counted off the request, so a request that shrinks fails this instead of
# quietly agreeing with it.
kSeries = 22 * 15
if nSeries != kSeries:
    print("    ONLY %d OF %d OBJECT-SERIES MEASURED -- a bound over the ones"
          " that happened to arrive is not the bound a client is promised"
          % (nSeries, kSeries))
    bad = True
# There is DELIBERATELY no "the worst case must be named" assertion here,
# though the empty "worst angle" line is what made the vacuous pass visible.
# whoA and whoR are also empty when every miss is exactly zero -- a server
# that is perfect rather than silent -- so asserting on them conflates "not
# measured" with "nothing missed". The --selftest control caught that on its
# first run, which is what a control is for: nSeries is the denominator, and
# it is the one that distinguishes the two.
# The verdict names WHICH failure. These are two different findings -- an
# advertisement that does not cover the server, and a sweep that did not
# measure what it claims to -- and printing the first line for the second
# sends a reader to look at the wrong thing. That happened on this leg's
# first run with the count assertion in it.
if overBound:
    print("    BAD: the server misses by more than it advertises")
elif bad:
    print("    BAD: the measurement is incomplete, so the bound is unproven")
else:
    print("    the advertisement covers it")
sys.exit(1 if bad else 0)
PY
}

leg_flag() {
  python3 - "$@" <<'PY'
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
}

# ---- --selftest: the three legs' own decisions, falsified ----------------
#
# Everything below grades a SERVER. This grades the GATE, against synthetic
# rows with no server at all, in about a second. A control plus eight
# injections, each required to red its OWN leg's assertion.
#
# It is here because two of these three legs used to pass on NOTHING. Both
# filter rows on err == 0, so a server that failed every object left the
# dictionaries empty, the loops ran zero times and the legs exited 0 --
# leg_zodiac printing its table header with nothing under it, leg_bound
# reporting "measured 0 deg/day ... the advertisement covers it", a perfect
# score for no data. Both were measured that way on 2026-09-20 before the
# count assertions went in, and this is what keeps them honest.
#
# The control is load-bearing: an assertion that reds unconditionally
# satisfies every injection while proving nothing.
if [ "$SELFTEST" = 1 ]; then
  st_ran=0; st_fail=0
  st_case() {   # st_case <ok|red> <desc> <command...>
    local want=$1 desc=$2; shift 2
    st_ran=$((st_ran + 1))
    if ( "$@" ) > /dev/null 2>&1; then
      [ "$want" = ok ] || { echo "  SELFTEST FAIL: $desc -- passed, must have failed"; st_fail=$((st_fail + 1)); return; }
    else
      [ "$want" = red ] || { echo "  SELFTEST FAIL: $desc -- failed, must have passed"; st_fail=$((st_fail + 1)); return; }
    fi
    echo "  ok: $desc"
  }
  ST="$SCRATCH/st"; mkdir -p "$ST"
  # The generator builds rows that are EXACTLY self-consistent: a longitude
  # linear in t at 1 deg/day, sampled at i/1024 day, whose five-point central
  # difference is 1.0 to the last bit, with the rate column set to 1.0. Every
  # miss is then identically zero, so any injection below is the only thing a
  # leg can be reacting to.
  st_gen() { python3 - "$1" "$2" <<'PY'
import os, sys
mode, d = sys.argv[1], sys.argv[2]
h = 1.0 / 1024.0
BODIES = ["10","301","199","299","499","5","6","7","8","9"]
POINTS = ["o:301/0/1","o:301/1/1","o:301/3/1","o:4/2/0","o:5/2/0","o:199/2/0","o:4/0/0","o:199/2/1"]
STARS  = ["s:Sirius","s:Polaris","s:Aldebaran","s:Vega"]
GRID   = BODIES + POINTS + STARS          # 22, as run_obs asks for them
ZOD    = ["10","301","199","499","5","9","s:Sirius","s:Aldebaran"]   # 8, as run() does

def rows(labels, rate=1.0, err=0, flags=0, bump=None):
    """Five self-consistent rows per label; bump(label) may add to the rate."""
    out = []
    for lab in labels:
        r = rate + (bump(lab) if bump else 0.0)
        for i in range(5):
            lon = i * h * rate
            out.append("%d %s %d 1 %d %s %s %s %s %s %s" % (
                i, lab, err, flags, float.hex(lon), float.hex(0.0), float.hex(0.0),
                float.hex(r), float.hex(0.0), float.hex(0.0)))
    return "\n".join(out) + "\n"

def write(name, text): open(os.path.join(d, name), "w").write(text)

# -- leg_zodiac's pair.
if mode == "zod-allerr":
    write("lahiri.txt", rows(ZOD, err=6)); write("citra.txt", rows(ZOD, err=6))
elif mode == "zod-worse":
    # true-citra misses by 1e-3 more than lahiri on one object: the very
    # defect this leg was written for (registry 2.5), a hundredfold over slack.
    write("lahiri.txt", rows(ZOD))
    write("citra.txt", rows(ZOD, bump=lambda l: 1e-3 if l == "301" else 0.0))
else:
    write("lahiri.txt", rows(ZOD)); write("citra.txt", rows(ZOD))

# -- leg_bound / leg_flag's grid: 15 files x 22 labels = 330 series.
adv = "ratebound=0.005/0.004"
if mode == "bound-noadv": adv = "engine=swiss"
write("welcome.txt", "serverName=selftest %s\n" % adv)
for k in range(15):
    err, flags, labels, bump = 0, 0, GRID, None
    if mode == "grid-allerr":       err = 6
    elif mode == "bound-over":      bump = lambda l: 0.9 if l == "301" else 0.0
    elif mode == "flag-unsound":    bump = lambda l: 0.9 if l == "10" else 0.0
    elif mode == "flag-allflagged": flags = 64
    elif mode == "flag-nomean":     labels = [l for l in GRID if not l.endswith("/0")]
    write("b-%02d.txt" % k, rows(labels, err=err, flags=flags, bump=bump))
PY
  }

  echo "== --selftest: the three legs' own decisions"
  G="$ST/g"; mkdir -p "$G"; st_gen control "$G"
  st_case ok  "control: leg_zodiac on self-consistent rows"  leg_zodiac "$G/lahiri.txt" "$G/citra.txt" 2451545.0
  st_case ok  "control: leg_bound on self-consistent rows"   leg_bound "$G/welcome.txt" "$G"/b-*.txt
  st_case ok  "control: leg_flag on self-consistent rows"    leg_flag "$G"/b-*.txt

  A="$ST/a"; mkdir -p "$A"; st_gen zod-allerr "$A"
  st_case red "leg_zodiac on an all-error answer (graded nothing)" leg_zodiac "$A/lahiri.txt" "$A/citra.txt" 2451545.0
  B="$ST/b"; mkdir -p "$B"; st_gen zod-worse "$B"
  st_case red "leg_zodiac when true-citra is worse than lahiri"    leg_zodiac "$B/lahiri.txt" "$B/citra.txt" 2451545.0

  C="$ST/c"; mkdir -p "$C"; st_gen grid-allerr "$C"
  st_case red "leg_bound on an all-error grid (measured nothing)"  leg_bound "$C/welcome.txt" "$C"/b-*.txt
  D="$ST/d"; mkdir -p "$D"; st_gen bound-over "$D"
  st_case red "leg_bound when a rate exceeds the advertisement"    leg_bound "$D/welcome.txt" "$D"/b-*.txt
  E="$ST/e"; mkdir -p "$E"; st_gen bound-noadv "$E"
  st_case red "leg_bound when WELCOME advertises no bound"         leg_bound "$E/welcome.txt" "$E"/b-*.txt

  F="$ST/f"; mkdir -p "$F"; st_gen flag-unsound "$F"
  st_case red "leg_flag when an object over tolerance is unflagged" leg_flag "$F"/b-*.txt
  H="$ST/h"; mkdir -p "$H"; st_gen flag-allflagged "$H"
  st_case red "leg_flag when every object is flagged (says nothing)" leg_flag "$H"/b-*.txt
  I="$ST/i"; mkdir -p "$I"; st_gen flag-nomean "$I"
  st_case red "leg_flag when the grid has no mean points"           leg_flag "$I"/b-*.txt

  [ "$st_ran" -eq 11 ] || { echo "SELFTEST FAIL: $st_ran cases ran, expected 11"; exit 1; }
  [ "$st_fail" = 0 ] || { echo "SELFTEST FAIL: $st_fail of $st_ran cases"; exit 1; }
  echo "RATES SELFTEST PASS: $st_ran cases (3 controls + 8 injections)"
  exit 0
fi

fail=0
for jd in 2451545.0 2461300.5; do
  run lahiri     "$jd" "$SCRATCH/lahiri.txt"
  run true-citra "$jd" "$SCRATCH/citra.txt"
  leg_zodiac "$SCRATCH/lahiri.txt" "$SCRATCH/citra.txt" "$jd" || fail=1
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
  # --precision 64 is NOT the default being restated. It is what 3.5a
  # requires the comparison to be made at, and this leg is the reason the
  # sentence exists: at f32 the distance column is quantised far above the
  # change the difference is trying to see -- Polaris's ULP is 3.26 AU
  # against a window change of 3.08e-5 -- so all five distances come back
  # bit-identical, the central difference is exactly zero, and every star
  # "misses" the bound by its own whole rate. The Sun's distance is
  # constant at f32 over this window too. Measured 2026-09-20, after the
  # other project's sweep reported an outlier that was this and not a rate.
  run_obs() {
    ./eph_wsclient --host 127.0.0.1 --port "$PORT" --quiet --precision 64 \
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
  #
  # THE 1800 EPOCH WAS 2378496.5 AND ANSWERED ALMOST NOTHING. It sits before
  # sepl_18's start -- ephsrv-golden.sh hit the same edge and says so about
  # 2378490.5 -- so the server refused it per object: at that epoch the
  # geocentric file graded 2 series of 22 and BOTH topocentric files graded
  # ZERO. 64 of the grid's 330 object-series, every one of them at the epoch
  # the comment above calls the worst case, and the bound was quietly being
  # taken over the 266 that survived. Nothing said so, because this leg
  # counted no denominator until 2026-09-20.
  #
  # AND THE FIX IS NOT "the first instant that answers". 2378500.5 grades
  # 22 of 22 and is WRONG for this leg: it is days inside the file, where
  # BuildWelcome()'s own bound sweep already documents that "Swiss falls
  # back to Moshier for some points and not others, still returning
  # success". Measured there, Polaris's reported distance rate is 7.3e-05
  # AU/day against -0.0072 differenced from its own distances -- and the
  # three observers disagree with EACH OTHER (7.3e-05 geo, -0.0151 Zurich,
  # -0.0070 Quito) where a topocentric correction to a star at 2.7e7 AU
  # cannot be more than a whisper. The differenced values agree across all
  # three to three figures, so it is the reported rate that is unstable
  # there, and swe_fixstar2_r reproduces 7.32288e-05 bit-for-bit outside
  # this server -- Swiss's number, faithfully relayed, at an instant no
  # bound should be set from. 2378600.5 is ~100 days in, grades 330 of 330,
  # and its worst is 1.7e-3 deg/day and 1.5e-3 AU/day.
  for jd in 2378600.5 2415020.5 2451545.0 2461300.5 2488069.5; do
    run_obs "$obs" "$jd" "$SCRATCH/b-$obs-$jd.txt"
  done
done
leg_bound "$SCRATCH/welcome.txt" "$SCRATCH"/b-*.txt || fail=1

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
leg_flag "$SCRATCH"/b-*.txt || fail=1

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
