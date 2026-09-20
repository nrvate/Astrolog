#!/bin/sh
# The chart-output behavior matrix: every text chart the console build can
# draw, over a pinned date and a fixed place, printed so two binaries can be
# byte-diffed.
#
# This exists because tools/switch-matrix.sh does NOT cover it. That harness
# prints each run's stderr and the settings file it saves -- it never renders
# a chart, so the whole of charts0-3.cpp, intrpret.cpp and the x*.cpp text
# paths sit outside it. The T5 sweep (work log item 143) converted 1,055
# sprintf sites, most of them in exactly that code, and the switch matrix was
# byte-identical while saying nothing at all about them.
#
#   tools/chart-matrix.sh ./old-astrolog > old.txt 2>&1
#   tools/chart-matrix.sh ./astrolog     > new.txt 2>&1
#   diff old.txt new.txt                 # empty = proven
#
# The date is pinned and "now" never reaches the output, so runs minutes
# apart still compare byte-for-byte. "_X" is mandatory: this build IS the
# X11 build and "=X" opens a window and sits there.
B=$1
[ -x "$B" ] || { echo "usage: $0 <astrolog-binary>"; exit 2; }
T=$(mktemp -d)
Q="-qa 6 15 1990 12:00 0 122W19 47N36"
Q2="-qa 3 9 1978 07:30 0 87W39 41N51"
A="$T/a.dat"; C="$T/b.dat"
env -u DISPLAY timeout 60 $B -n $Q -o "$A" _X </dev/null >/dev/null 2>&1
env -u DISPLAY timeout 60 $B -n $Q2 -o "$C" _X </dev/null >/dev/null 2>&1
run() {
  # Both the header and the output get the temp path normalized: the chart
  # files the relationship runs need live under mktemp -d, and its random
  # name would otherwise be the whole diff between two runs.
  echo "== $*" | sed "s|$T|TMP|g"
  env -u DISPLAY timeout 60 $B -n $Q "$@" _X </dev/null 2>&1 | sed "s|$T|TMP|g"
}
# Single-chart text modes.
for sw in -v -v0 -w -w0 -g -g0 -ga -gp -m -m0 -Z -Z0 -Zd -S -j -j0 -L -L0 \
  -K -d -D -E -Ey -7 -8 -I -I0 -l -l0 -k -k0; do
  run $sw
done
# Transits and progressions, which reformat every position again.
run -T 6 15 1991
run -Tt 6 15 1991 12:00
run -p 6 15 2020
run -P 12
# The same modes with seconds, sidereal, and a 3D house system on, since
# each changes every formatted position string in the chart.
for sw in -v -w -g -j -L -7 -I; do
  run $sw -b0
  run $sw -s
  run $sw -c Campanus -c3
done
# FIXED STARS, which nothing in this harness could see until 2026-09-19.
# The four matrices were all byte-identical across the change that moved
# every -Ys star by 31.5" and put the four astrometric binaries on their
# orbits -- not because nothing regressed, but because no leg in any of
# them listed a star or cast -Ys. An inert surface diffs to zero exactly
# like a clean one.
#
# -b0 is not decoration here and is the whole reason the tropical leg
# earns its place: the binary-star offsets are sub-arcsecond to ~2", so at
# the arcminute resolution every other leg prints, Sirius and Procyon are
# byte-identical before and after their orbits were applied. At seconds
# they move by 1" and Vega, which has no orbit, does not.
#
# The three legs are three different code paths, not one with options:
# tropical is the orbit arithmetic alone, -s adds the ayanamsa, and -Ys
# adds ApplySidPlaneLocal() -- the plane-2 rotation that was the 31.5".
run -b0 -U
run -b0 -U -s
run -b0 -U -s -Ys
# The same plane through the PLANETS, since -Ys reaches FSwissPlanet() by
# a different route than it reaches FSwissStar() and the two disagreeing
# about the origin is the defect that was fixed.
run -b0 -v -s -Ys
# TOPOCENTRIC, which nothing here covered either, and which is where the
# Moon's named points were found answering a geocentric position beside a
# rate that had moved (registry 2.9). -R1 unrestricts, because Lilith --
# the lunar apogee, the body the defect was measured on -- is restricted by
# default and a leg that cannot see it would have passed throughout.
#
# The SECOND place is not a duplicate: a topocentric position is a function
# of the site, so one place cannot distinguish "the site is applied" from
# "the site is ignored". Quito is 10,000 km from Seattle and near the
# equator, where the parallax is largest.
run -YV -R1 -b0 -v
echo "== -YV -R1 -b0 -v (Quito)"
env -u DISPLAY timeout 60 $B -n -qa 6 15 1990 12:00 0 78W30 0N13 \
  -YV -R1 -b0 -v _X </dev/null 2>&1 | sed "s|$T|TMP|g"
# Relationship charts, which need two chart files rather than two -q blocks.
for sw in -r -rc -rm -r0 -rt; do
  run $sw "$A" "$C" -v
  run $sw "$A" "$C" -g
done
run -rd "$A" "$C"
run -rb "$A" "$C"
# Interpretation text, the largest single block of converted sites.
for sw in -v -w -j -7; do
  run $sw -I
done
# A polar chart, because everything above this line is Seattle and Chicago
# and the house code behaves differently toward the pole. Work log items
# 157, 158 and 160 changed house math for six systems and moved NOTHING in
# this matrix, which was correct but meant the differential could not have
# caught a regression in that work. Longyearbyen at 78N13 is in the shipped
# atlas, and December is when the sun-declination constructions degenerate.
QP="-qa 12 20 1976 12:00 0 15:38E 78:13N"
for hs in 0 3 5 8 13 20; do
  echo "== polar -c $hs"
  env -u DISPLAY timeout 60 $B -n $QP -c $hs -w _X </dev/null 2>&1 |
    sed "s|$T|TMP|g"
done
rm -rf $T
