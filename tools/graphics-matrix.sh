#!/bin/sh
# The graphics-output behavior matrix: every graphics chart mode, option and
# writer the console build can produce, over a pinned date and place, printed
# as one checksum per render so two binaries can be byte-diffed.
#
#   tools/graphics-matrix.sh ./old-astrolog > old.txt 2>&1
#   tools/graphics-matrix.sh ./astrolog     > new.txt 2>&1
#   diff old.txt new.txt                    # empty = proven
#
# This exists because the other two matrices are both blind to it.
# tools/switch-matrix.sh prints stderr and the settings a run saves and never
# renders; tools/chart-matrix.sh renders only *text* charts. The whole of
# xcharts0-2.cpp, xgeneral.cpp, xdevice.cpp and xscreen.cpp sat outside both,
# and that is where a change to drawing code actually lands. Phase 2's P6
# built this comparison by hand for one switch family (work log items
# 102-104); this is the same idea, kept.
#
# The invocation list is the one tools/asan-sweep.sh drives, for the same
# reason it chose it: every chart mode bare, then every option on three chart
# types that draw very differently, then each output writer. What that sweep
# asks is "did anything corrupt memory"; what this asks is "did the picture
# change".
#
# Three things here are deliberate:
#
#   - Each render's checksum is printed, not the file, so the output is a few
#     hundred lines rather than a few hundred megabytes.
#   - A render that produces NO file prints "MISSING" and its first stderr
#     line, and the run's tail counts them. A harness whose invocations all
#     fail diffs to zero and reads exactly like a proof -- that has happened
#     twice in this project (see CLAUDE.md, "A harness proves nothing until
#     you sabotage it either").
#   - The date is pinned and the output path is fixed, so two runs minutes
#     apart compare byte for byte. Verify that before trusting a clean diff:
#     run it twice against the SAME binary and expect an empty diff.
set -e
cd "$(dirname "$0")/.." || exit 1
B=$1
[ -x "$B" ] || { echo "usage: $0 <astrolog-binary> "; exit 2; }
CFG=${GRAPHICS_MATRIX_CFG:--i nrvate.as}
Q="-qb 7 4 1976 12 0 8 122:19:55W 47:36:22N"
# Fixed paths, not mktemp: the PostScript writer puts its output file name
# in a %%Title comment, so a random directory made six renders differ
# between two runs of the SAME binary. Determinism is the whole product
# here. Kept distinctive so they cannot collide with anything real.
# A fixed path also means two runs of this script cannot overlap: the
# "rm -rf" below would delete the other run's working directory
# mid-flight, and the damage would surface as a DIFF -- a false
# regression, which is the most expensive kind of wrong answer a
# differential can give. That is the same shape as the nine hard-coded
# temp filenames in qttest.cpp that let two suites delete each other's
# files mid-measurement (work log item, 2026-09-01), and this tree
# routinely has two sessions in it.
#
# So: refuse rather than corrupt. The guard is a pid file, checked with
# kill -0, so a run killed halfway leaves nothing that blocks the next
# one. It does not change a single output byte -- verified by diffing a
# full 224-render run before and after adding it.
T=/tmp/astrolog-graphics-matrix
if [ -f "$T/.pid" ] && kill -0 "$(cat "$T/.pid" 2>/dev/null)" 2>/dev/null; then
  echo "== another graphics-matrix.sh (pid $(cat "$T/.pid")) is using $T."
  echo "== The path is fixed on purpose -- see above -- so two runs cannot"
  echo "== share it. Wait for that one, or point it elsewhere."
  exit 1
fi
rm -rf "$T"; mkdir -p "$T"; echo $$ > "$T/.pid"
O="$T/o"
missing=0
runs=0

g() {
  runs=$((runs+1))
  # The multi-wheel runs name chart files under mktemp -d, and its random
  # directory would otherwise be the whole diff between two runs.
  echo "== $*" | sed "s|$T|TMP|g"
  rm -f "$O"*
  # shellcheck disable=SC2086
  env -u DISPLAY timeout 120 $B $CFG $Q "$@" -Xo "$O" </dev/null \
    >"$T/log" 2>&1 || true
  # The vector writers append their own extension in some builds, so match
  # the stem rather than the exact name.
  out=$(ls "$O"* 2>/dev/null | head -1)
  if [ -n "$out" ] && [ -s "$out" ]; then
    echo "  $(md5sum < "$out" | cut -d' ' -f1)  $(wc -c < "$out") bytes"
  else
    missing=$((missing+1))
    echo "  MISSING  $(head -1 "$T/log" | sed "s|$T|TMP|g")"
  fi
}

for m in "" -XX -XX0 -XW -XW0 -XG -XG0 -XP -XP0 -XZ \
         -g -g0 -Z -Z0 -L -L0 -7 -l -d -E -j -8 -5 -k0 -v -w -m -S; do
  g $m
done
for base in "" -XG -XW; do
  for o in "-Xv 0" "-Xv 1" "-Xv 2" "-Xv 3" "-Xv 4" "-Xv 5" "-Xv 6" \
           "-Xv 7" -Xv0 -XA -XL "-XL 1" "-XL 3" "-XL 5" -XU "-XU 0" \
           "-XU 2" "-XU 3" -XUx -XC -XJ -X8 -Xi -Xt -Xu -Xx -Xx0 -Xl \
           -Xe -Xj -XQ -XQ0 -Xr -Xm -X3 -XN -XF "-Xs 100" "-Xs 400" \
           "-XS 100" "-XS 400" "-Xw 400 300" "-Xw 2000 1500" "-X1 5" \
           "-X2 9" "-XI0 0 0" "-XI0 100 1" "-Xk 5" "-Xkv 9" "-XE 1 20" \
           "-XE0 1 5" "-XE3 1 5"; do
    g $base $o
  done
done
# -XM1/-XM3/-XM6 are prefix forms wanting extra arguments; they belong to
# the switch matrix, not here, since they error before rendering.
for w in -Xb -Xbw -Xbb -Xbp -Xbn -Xbc -Xbv -Xba -XM \
         -XV -Xp -Xp0; do
  g $w; g $w -XG; g $w -g
done
# The multi-wheel charts, which need extra chart slots loaded and so are
# not reachable from a bare switch. Work log item 147's fix lived here.
A="$T/a.dat"
env -u DISPLAY timeout 60 $B $CFG -n $Q -o "$A" _X </dev/null >/dev/null 2>&1
for r in 3 4 5 6; do
  a=""; j=2
  while [ $j -le $r ]; do a="$a -i$j $A"; j=$((j+1)); done
  # shellcheck disable=SC2086
  g -r$r $a
done

rm -rf "$T"
echo "== $runs renders, $missing produced no file"
# And exit nonzero if any did, which this script did NOT do until
# 2026-09-01. A render that produced no file compares equal to another
# that also produced none, so a run where every invocation failed diffs
# to zero against any other such run and reads exactly like a proof.
# Measured: pointing GRAPHICS_MATRIX_CFG at a malformed settings file
# makes all 224 renders fail, and the old script reported that as
# success. The header above says a harness proves nothing until you
# sabotage it; this is the harness failing its own rule.
if [ "$missing" -gt 0 ]; then
  echo "== NOT A VALID BASELINE: $missing of $runs renders produced no file."
  echo "== Check the config resolves the ephemeris (GRAPHICS_MATRIX_CFG"
  echo "== defaults to -i nrvate.as) and that the binary path is short."
  exit 1
fi
exit 0
