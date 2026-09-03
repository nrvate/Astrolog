#!/bin/sh
# Build with UndefinedBehaviorSanitizer and sweep the chart and graphics
# matrices for undefined behaviour.
#
#   tools/ubsan-sweep.sh [chart|graphics]     # default: both
#
# A DIFFERENT NET FROM tools/asan-sweep.sh, not a cheaper one. ASan finds
# memory errors -- out of bounds, use after free. UBSan finds signed
# overflow, invalid shifts, misaligned access, out-of-range enum and
# float-to-int conversions. This codebase is overwhelmingly arithmetic,
# which is exactly UBSan's territory and exactly what nothing here looked
# at until 2026-09-03.
#
# TWO THINGS ABOUT THE BUILD ARE LOAD-BEARING, and getting them wrong
# produces a binary with no sanitizer in it that reports a clean sweep:
#
#   1. The Makefile links with LIBS, not LDFLAGS. Passing -fsanitize to
#      LDFLAGS is silently ignored, and LIBS also carries -s, which strips
#      the binary so even "nm" looks empty afterwards.
#   2. The Makefile ignores OBJDIR -- OBJS is patsubst'd into the tree
#      root. So a build "into obj-ubsan" relinks the EXISTING objects,
#      instrumented or not, and overwrites ./astrolog's objects on the way.
#
# The first attempt at this hit both and swept 366 invocations of an
# uninstrumented binary. It is verified here instead of assumed: the
# script checks for libubsan before running anything.
#
# Because of (2) this leaves the tree's .o files instrumented. It runs
# "make clean-console" at the end for that reason.
set -e

want=${1:-both}
cd "$(dirname "$0")/.."

echo "== building with -fsanitize=undefined"
make NAME=astrolog-ubsan \
  CPPFLAGS="-MMD -MP -O1 -g -std=gnu++17 -fsanitize=undefined \
    -Wno-write-strings -Wno-narrowing -Wno-comment" \
  LIBS="-lm -lX11 -ldl -fsanitize=undefined" -j4 >/dev/null

ldd ./astrolog-ubsan 2>/dev/null | grep -qi ubsan || {
  echo "   astrolog-ubsan has no libubsan -- the build did not instrument."
  echo "   Check that LIBS (not LDFLAGS) carries -fsanitize=undefined."
  exit 1; }
echo "   instrumented: libubsan linked"

UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0
export UBSAN_OPTIONS
rc=0
run() {
  echo "== $1"
  out=$($2 2>&1 | grep -aoE "runtime error: [^(]*" | sort | uniq -c | sort -rn || true)
  if [ -n "$out" ]; then printf '%s\n' "$out" | sed 's/^/   /'; rc=1
  else echo "   clean"; fi
}
[ "$want" = both ] || [ "$want" = chart ] && \
  run "chart matrix (142 invocations)" "tools/chart-matrix.sh ./astrolog-ubsan"
[ "$want" = both ] || [ "$want" = graphics ] && \
  run "graphics matrix (224 renders)" "env GRAPHICS_MATRIX_CFG='-Yi1 ephem' tools/graphics-matrix.sh ./astrolog-ubsan"

echo "== restoring the tree's objects (the Makefile ignores OBJDIR)"
make clean-console >/dev/null 2>&1 || true
exit $rc
