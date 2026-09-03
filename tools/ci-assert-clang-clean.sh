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

# Resolve each warning to a file that REALLY EXISTS before classifying it.
#
# The build is "make qt -j4 | tee build.log", so two compilers can write
# the same line and their output interleaves mid-line. A nightly failed
# on exactly that: a stray character was spliced onto the front of a
# warning, making it read
#
#   aswemplan.cpp:882:11: warning: 'sprintf' is deprecated
#
# There is no aswemplan.cpp. It is swemplan.cpp, which is vendored Swiss
# Ephemeris and deliberately excluded -- but "aswemplan" does not start
# with "swe", so the exclusion missed it and the check reported a warning
# in "this codebase's own sources" that did not exist. The same log line
# carried the mangled source text, "t      sprintf(...)", which is the
# same splice one line further down. It passed thirteen minutes earlier
# on the same code; it was luck, not a change.
#
# So a name is trusted only if the file is there. If it is not, and a
# real source file is a suffix of it, that is the splice and the real
# name is used. Anything left unresolved is reported by name rather than
# silently binned, because an unknown file in a build log is worth
# seeing.
srcs=$(ls *.cpp *.h 2>/dev/null)
resolve_src() {
  for s in $srcs; do [ "$1" = "$s" ] && { echo "$s"; return; }; done
  for s in $srcs; do case $1 in *"$s") echo "$s"; return ;; esac; done
  echo "$1"
}

raw=$(grep -aoE '[A-Za-z0-9_]+\.(cpp|h):[0-9]+:[0-9]+: warning:' "$log" \
  | sed 's/:.*//' | sort -u || true)
ours=""
for r in $raw; do
  real=$(resolve_src "$r")
  case $real in
    swe*|placalc*) ;;                 # vendored, counted below not failed on
    *) ours="$ours$real
" ;;
  esac
done
ours=$(printf '%s' "$ours" | grep -v '^$' | sort -u || true)
if [ -n "$ours" ]; then
  echo "clang warnings in this codebase's own sources:"
  printf '%s\n' "$ours" | sed 's/^/  /'
  for o in $ours; do
    grep -aE "(^|[^A-Za-z0-9_])$o:[0-9]+:[0-9]+: warning:" "$log" \
      | head -3 | sed 's/^/    /'
  done
  exit 1
fi

# Counted the same splice-tolerant way, so an interleaved line still lands
# in the vendored tally instead of vanishing from both.
n=0
for r in $raw; do
  real=$(resolve_src "$r")
  case $real in
    swe*|placalc*)
      c=$(grep -acE "(^|[^A-Za-z0-9_])$r:[0-9]+:[0-9]+: warning:" "$log" || true)
      n=$((n + ${c:-0})) ;;
  esac
done
echo "clang clean: 0 warnings in our sources, ${n:-0} in vendored Swiss Ephemeris"
if [ -n "$expect" ] && [ "${n:-0}" -gt "$expect" ]; then
  echo "vendored warnings grew past $expect -- did something get added to swe*?"
  exit 1
fi
