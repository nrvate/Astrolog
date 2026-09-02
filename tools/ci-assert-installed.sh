#!/bin/sh
# Assert an installed Astrolog finds its data from an unrelated directory.
#
#   tools/ci-assert-installed.sh ~/.local/bin/astrolog
#
# QT_CI_PLAN.md item 4.5, applied to "make install" rather than to a
# package. The install puts two wrappers on PATH and leaves every data
# file -- the ephemeris, the atlas, the fonts -- in the checkout, so the
# thing that can break is data resolution, not linking. Astrolog builds
# its ephemeris search path from the executable's own directory, which is
# why a baseline binary moved out of the repository root silently reads a
# different ephemeris (item 7.2b) and why this runs from "/".
#
# ASSERT A BODY THAT CANNOT COMPUTE WITHOUT THE FILES. Astrolog falls back
# to the Moshier formulas silently: with ephem/ deleted entirely the Sun
# still reads 24Gem07'46", and only its velocity moves, in the seventh
# decimal. A swetest-style "^Sun +24Gem07" check therefore passes on an
# install with no ephemeris in it at all -- that was the assertion this
# project specified for two drafts before anyone removed the files and
# looked. Chiron reads 0Ari00'00" when the ephemeris is missing, which is
# binary and unmistakable.
#
# Exit 0 when the binary runs from an unrelated cwd and Chiron is not
# 0Ari00.
set -eu

bin=${1:?usage: ci-assert-installed.sh <path to installed binary>}
case $bin in /*) ;; *) bin=$(cd "$(dirname "$bin")" && pwd)/$(basename "$bin");; esac

if [ ! -x "$bin" ]; then
  echo "NOT INSTALLED: $bin is not an executable file."
  exit 1
fi

out=$(cd / && "$bin" -qa 6 15 1990 12:00 0 122W19 47N36 -R1 _X 2>&1) || true

if [ -z "$out" ]; then
  echo "NOT INSTALLED: $bin produced no output at all from /."
  exit 1
fi

chiron=$(printf '%s\n' "$out" | grep -E '^Chir' | head -1 || true)
if [ -z "$chiron" ]; then
  echo "NOT INSTALLED: no Chiron line in the chart. Output began:"
  printf '%s\n' "$out" | head -5 | sed 's/^/  /'
  exit 1
fi

case $chiron in
  *0Ari00*)
    echo "EPHEMERIS NOT FOUND: $chiron"
    echo "0Ari00'00\" is Astrolog's no-ephemeris answer. The binary ran but"
    echo "could not resolve its data files from the executable's directory."
    exit 1
    ;;
esac

echo "installed ok: run from /, $chiron"
