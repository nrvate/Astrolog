#!/bin/sh
# Influence-computation matrix: the -j/-j0/-7 charts for a fixed date
# under six rulership-restriction states, for byte-diffing two binaries
# the way tools/switch-matrix.sh does for the parser. Proved D2 (the
# RULERSYS table in intrpret.cpp; work log item 79).
B=$1
# The settings this runs under. It hardcoded "-i nrvate.as" until
# 2026-09-02, which is the maintainer's own file and points -Yi1 at a NAS
# mount that exists on exactly one machine -- so anywhere else, including
# every CI runner, this harness silently computed against a different
# ephemeris than the one it was written for. tools/graphics-matrix.sh
# already had this lever; now all the config-taking matrices do.
#
#   INFLUENCE_MATRIX_CFG="-Yi1 ephem" tools/influence-matrix.sh ./astrolog
#
# Both sides of a differential must use the same value, which is the same
# rule as the binary's path length: the output is not comparable across
# configurations. Measured 2026-09-02: switching this changes real numbers.
CFG=${INFLUENCE_MATRIX_CFG:--i nrvate.as}
for r7 in "0 0 0 0 0" "1 0 1 1 1" "1 1 0 1 1" "0 1 1 1 1" "1 1 1 0 0" "0 0 1 0 1"; do
  for chart in "-j" "-j0" "-j -J" "-7"; do
    echo "== -YR7 $r7 $chart"
    env -u DISPLAY $B $CFG -qd 8 15 2020 -YR7 $r7 $chart _X 2>&1
  done
done
