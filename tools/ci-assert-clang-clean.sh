#!/bin/sh
# Assert that a clang build log contains no warning from Astrolog's own
# sources. Vendored Swiss Ephemeris files are counted and reported, not
# failed on.
#
#   tools/ci-assert-clang-clean.sh <build.log> [expected-vendored-count]
#
# tools/warning_audit.py covers five GCC builds and knows nothing about
# clang, so the macOS job compiled 260 warnings a night that nobody read.
# All 260 were 'sprintf' is deprecated, and all of them were in sweph.cpp,
# swephlib.cpp, swecl.cpp and their siblings -- zero in this codebase's
# own files, because the 185 raw sprintf calls there became sprintf2.
#
# Those 260 are not ours to fix here. Astrolog vendors and adapts its own
# copy of Swiss Ephemeris, which has diverged from any upstream by
# thousands of lines -- sweph.cpp is 8,621 lines against nrvate/swisseph's
# 9,267 -- so a fix upstream does not reach this tree, and re-vendoring is
# a change to the numeric core rather than a warning cleanup.
#
# What IS ours is that the number does not quietly grow, and that no
# warning ever appears in a file we wrote. A blanket
# -Wno-deprecated-declarations would have hidden both.
set -e

log=${1:?usage: ci-assert-clang-clean.sh <build.log> [expected-vendored]}
expect=${2:-}
[ -f "$log" ] || { echo "no such log: $log"; exit 2; }

ours=$(grep -aE '^[a-z0-9_]+\.(cpp|h):[0-9]+:[0-9]+: warning:' "$log" \
  | grep -avE '^(swe|placalc)' | sed 's/:.*//' | sort -u || true)
if [ -n "$ours" ]; then
  echo "clang warnings in this codebase's own sources:"
  printf '%s\n' "$ours" | sed 's/^/  /'
  grep -aE '^[a-z0-9_]+\.(cpp|h):[0-9]+:[0-9]+: warning:' "$log" \
    | grep -avE '^(swe|placalc)' | head -10 | sed 's/^/    /'
  exit 1
fi

n=$(grep -acE '^(swe|placalc)[a-z0-9_]*\.(cpp|h):[0-9]+:[0-9]+: warning:' "$log" || true)
echo "clang clean: 0 warnings in our sources, ${n:-0} in vendored Swiss Ephemeris"
if [ -n "$expect" ] && [ "${n:-0}" -gt "$expect" ]; then
  echo "vendored warnings grew past $expect -- did something get added to swe*?"
  exit 1
fi
