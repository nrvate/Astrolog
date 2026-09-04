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

# The Qt suite is the other half of the apparatus and reaches different
# code: measured, xdevice.cpp is 46.6% from the matrices and 14.6% from
# the suite, while express.cpp is 20.5% from the matrices and 38.6% from
# the suite. Neither substitutes for the other, so the interesting number
# is what BOTH miss. Its own OBJDIR, so its .gcda cannot collide with the
# console build's, which land in the tree root.
if [ "${COVERAGE_SKIP_SUITE:-0}" != "1" ] && [ -f Makefile.qt.test ]; then
  echo "== building and running the Qt suite under coverage"
  make -f Makefile.qt.test NAME=astrolog-qt-cov OBJDIR=obj-qt-cov \
    CPPFLAGS='-MMD -MP -DQT -DQTTEST -O0 -g -fPIC -std=gnu++17 --coverage \
      -Wno-write-strings -Wno-narrowing -Wno-comment $(QT_CFLAGS)' \
    LDEXTRA='--coverage' -j4 >/dev/null 2>&1 || true
  if [ -x ./astrolog-qt-cov ]; then
    rm -f obj-qt-cov/*.gcda
    QTTESTBIN=./astrolog-qt-cov ./run-qt-tests.sh >"$out/suite.txt" 2>&1 || true
    gcov -n -o obj-qt-cov obj-qt-cov/*.gcda 2>/dev/null | awk '
      /^File/ { f=$2; gsub(/'"'"'/,"",f) }
      /^Lines executed/ { split($0,a,":"); split(a[2],b,"% of "); print b[1]"\t"b[2]"\t"f }
    ' | grep -vE "/usr/|\.h$" | sort -n > "$out/coverage-suite.txt"
    echo "== executed by NEITHER the matrices NOR the suite"
    awk -F'\t' 'NR==FNR{p[$3]=$1; l[$3]=$2; next}
                 ($3 in p) && p[$3]+0==0 && $1+0==0 { print $3 }' \
      "$out/coverage-suite.txt" "$out/coverage.txt" \
      | LC_ALL=C sort > "$out/dead.txt"
    awk -F'\t' 'NR==FNR{p[$3]=$1; l[$3]=$2; next}
                 ($3 in p) && p[$3]+0==0 && $1+0==0 {
                   printf "   %6s lines  %s\n", l[$3], $3; t+=l[$3] }
                 END{ if(t) printf "   TOTAL: %d lines nothing here executes\n", t
                      else print "   (none)" }' \
      "$out/coverage-suite.txt" "$out/coverage.txt"

    # Exact, like every other count in this project, and the expected set
    # is EMPTY since 2026-09-04. It used to be placalc.cpp and
    # placalc2.cpp, 698 lines unreachable because "=0b" in the shipped
    # astrolog.as locked the backend out and nothing cleared it -- the
    # first run of this report found them, and the maintainer removed
    # them rather than test them. A file JOINING the set is the signal:
    # either new code arrived with no net behind it, or a harness stopped
    # reaching code it used to. LC_ALL=C on BOTH sides, so two sorts
    # cannot disagree about an identical set.
    : > "$out/dead.want"
    if ! diff -q "$out/dead.want" "$out/dead.txt" >/dev/null 2>&1; then
      echo "   THE UNTESTED SET CHANGED:"
      diff "$out/dead.want" "$out/dead.txt" | sed 's/^/     /'
      echo "   < expected, > measured. Update the expected set in this"
      echo "   script only after saying in the commit message which net"
      echo "   changed and why."
      COVERAGE_DEAD_CHANGED=1
    else
      echo "   unchanged: exactly the two known-unreachable files"
    fi
    rm -rf obj-qt-cov ./astrolog-qt-cov
  else
    echo "   Qt suite build failed; matrices-only report"
  fi
fi

echo "== restoring the tree"
make clean-console >/dev/null 2>&1 || true
rm -f ./*.gcda ./*.gcno ./astrolog-cov
make -j4 >/dev/null
[ -x ./astrolog ] || { echo "   FAILED to rebuild ./astrolog"; exit 1; }
echo "   ./astrolog rebuilt uninstrumented"
echo "report in $out/coverage.txt"
[ "${COVERAGE_DEAD_CHANGED:-0}" = "0" ] || exit 1
