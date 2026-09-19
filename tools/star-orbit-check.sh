#!/bin/sh
# tools/star-orbit-check.sh -- the four astrometric binaries, end to end.
#
#   tools/star-orbit-check.sh
#
# EPHEMERIS_ACCURACY_REGISTRY.md 2.4. Two legs, because they fail differently
# and neither substitutes for the other:
#
#   1. tools/star-orbit-check.py -- the ARITHMETIC, against ORB6's own
#      published ephemeris and against sefstars.txt's own alpha Cen A-to-B
#      separation at the Hipparcos epoch. No engine of ours is involved in
#      either reference.
#
#   2. tools/star-orbit-apply.cpp -- ephsrv/ephstarorb.h itself, through Swiss,
#      against Ephemeris Prometheia's published offsets. This is the leg that
#      covers the frame work: the tangent basis, the rotation into the frame
#      the request asked for, the star placed from another's line.
#
# Leg 2 compiles against the VENDORED Swiss sources, which is the no-context
# branch of the header -- the one Astrolog's own star path takes. The server
# compiles the context-taking branch, and "make ephsrv" is what covers that.
#
# The star data has to be on the path, and sefstars.txt lives at the top of
# this tree rather than in ephem/, so the path is both.
set -e
cd "$(dirname "$0")/.."

echo "== the orbit arithmetic, against ORB6's own ephemeris and sefstars.txt"
python3 tools/star-orbit-check.py

echo
echo "== the header itself, against the other engine's published offsets"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
g++ -O2 -std=gnu++17 -w -I. -o "$tmp/star-orbit-apply" tools/star-orbit-apply.cpp \
  swecl.cpp swedate.cpp swehouse.cpp swejpl.cpp swemmoon.cpp swemplan.cpp \
  sweph.cpp swephlib.cpp -lm
"$tmp/star-orbit-apply" ".:ephem"

echo
echo "STAR ORBIT CHECK PASS: both legs"
