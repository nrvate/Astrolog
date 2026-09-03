#!/bin/sh
# What do the four differential matrices actually EXECUTE?
#
#   tools/coverage-report.sh [outdir]
#
# Every other net in this project answers "did this change?". This one
# answers "is this code reached at all?", which is a different question and
# the one that catches a harness entry that has been inert for years.
#
# It exists because of one: tools/graphics-matrix.sh carried "-XE 1 20"
# from the day it was written, and that renders byte-identically to no -XE
# at all -- the asteroid loop stops at the first body it cannot compute,
# and asteroid 9 has no ephemeris file. Nothing noticed, because an inert
# entry still diffs to zero, which is what a passing differential looks
# like. Coverage is how you find the next one without waiting for luck.
#
# SLOW. The instrumented build is -O0 and the switch matrix alone is 529
# invocations; budget tens of minutes, not seconds. Pre-release, never
# pre-commit.
#
# TWO THINGS ABOUT THE BUILD ARE LOAD-BEARING, both learned by
# tools/ubsan-sweep.sh hitting them first:
#
#   1. The Makefile links with LIBS, not LDFLAGS. --coverage in LDFLAGS is
#      silently ignored and the run reports nothing.
#   2. The Makefile ignores OBJDIR, so this instruments the tree's own .o
#      files. That is why it runs "make clean-console" and rebuilds
#      ./astrolog at the end -- WITHOUT that, every later test in the tree
#      runs against an -O0 instrumented binary and nobody is told.
set -eu

out=${1:-out/coverage}
cd "$(dirname "$0")/.."
mkdir -p "$out"

echo "== building an instrumented console binary"
make clean-console >/dev/null 2>&1 || true
rm -f ./*.gcda ./*.gcno
make NAME=astrolog-cov \
  CPPFLAGS="-MMD -MP -O0 -g -std=gnu++17 --coverage \
    -Wno-write-strings -Wno-narrowing -Wno-comment" \
  LIBS="-lm -lX11 -ldl --coverage" -j4 >/dev/null

# Verified, not assumed: an uninstrumented binary produces no .gcno and
# would report every file at 0%, which reads like a dramatic finding.
[ "$(ls ./*.gcno 2>/dev/null | wc -l)" -gt 0 ] || {
  echo "   no .gcno files -- the build did not instrument."
  echo "   Check that LIBS (not LDFLAGS) carries --coverage."; exit 1; }

echo "== running all four matrices against it (this is the slow part)"
tools/chart-matrix.sh ./astrolog-cov >"$out/chart.txt" 2>&1 || true
GRAPHICS_MATRIX_CFG='-Yi1 ephem' tools/graphics-matrix.sh ./astrolog-cov \
  >"$out/graphics.txt" 2>&1 || true
INFLUENCE_MATRIX_CFG='-Yi1 ephem' tools/influence-matrix.sh ./astrolog-cov \
  >"$out/influence.txt" 2>&1 || true
tools/switch-matrix.sh ./astrolog-cov >"$out/switch.txt" 2>&1 || true

echo "== coverage, least-covered first"
gcov -n ./*.gcda 2>/dev/null | awk '
  /^File/ { f=$2; gsub(/'"'"'/,"",f) }
  /^Lines executed/ { split($0,a,":"); split(a[2],b,"% of "); print b[1]"\t"b[2]"\t"f }
' | grep -vE "/usr/|\.h$" | sort -n > "$out/coverage.txt"
awk -F'\t' '{printf "   %6s%%  %6s lines  %s\n", $1, $2, $3}' "$out/coverage.txt"

echo "== restoring the tree"
make clean-console >/dev/null 2>&1 || true
rm -f ./*.gcda ./*.gcno ./astrolog-cov
make -j4 >/dev/null
[ -x ./astrolog ] || { echo "   FAILED to rebuild ./astrolog"; exit 1; }
echo "   ./astrolog rebuilt uninstrumented"
echo "report in $out/coverage.txt"
