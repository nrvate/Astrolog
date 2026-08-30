#!/bin/sh
# Influence-computation matrix: the -j/-j0/-7 charts for a fixed date
# under six rulership-restriction states, for byte-diffing two binaries
# the way tools/switch-matrix.sh does for the parser. Proved D2 (the
# RULERSYS table in intrpret.cpp; work log item 79).
B=$1
for r7 in "0 0 0 0 0" "1 0 1 1 1" "1 1 0 1 1" "0 1 1 1 1" "1 1 1 0 0" "0 0 1 0 1"; do
  for chart in "-j" "-j0" "-j -J" "-7"; do
    echo "== -YR7 $r7 $chart"
    env -u DISPLAY $B -i nrvate.as -qd 8 15 2020 -YR7 $r7 $chart _X 2>&1
  done
done
