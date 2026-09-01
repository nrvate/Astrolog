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
rm -rf $T
