#!/bin/sh
# The kind-4 elements fixture: the four cases of the elements-rule drop,
# computed through the vendored Swiss Ephemeris and printed as numbers.
#
#   tools/kind4-fixture.sh [outdir]      # default: ./out/kind4
#
# The setup is chosen so that the answer is a pure function of the
# elements and nothing else: geometric (no corrections), rectangular, the
# mean ecliptic and equinox of J2000, seen from the orbit's own origin. No
# ephemeris position enters, so this is two-body mechanics under the four
# sentences of 3.5a and nothing more -- which is what makes it checkable
# against an engine that shares no code with this one.
#
# KNOWN DIFFERENCE, so a cross-check does not chase it. 3.5a states the
# Gaussian constant as 0.01720209895 rad/day. Swiss carries it as a
# decimal truncated to ten significant figures in DEGREES per day, which
# is 1.446e-12 relative. That enters only through the mean motion, so it
# is absent wherever no mean motion is added, and otherwise accumulates as
# n*t times that: invisible for the Hamburg points this feature serves
# (under half an orbit per century, ~2e-7 arcsec) and 0.3 mas for case D,
# which is contrived to amplify it with 173 orbits. Not corrected here:
# the constant is upstream Swiss's, shared with every other Swiss
# consumer, and changing it would make this program the only one that
# disagrees with the rest about where Vulcan is.
set -eu
cd "$(dirname "$0")/.."
out=${1:-out/kind4}
mkdir -p "$out"

cat > "$out/seorbel.txt" <<'EOF'
# epoch, equinox, M, a, e, w, node, i, name[, geo]
# A: nTerms 1 -- M is the mean anomaly at epoch, the body advances at n.
2415020.0, J2000, 10, 41, 0.02, 20, 30, 1.5, CaseA
# B: the corrected rule's own case. On the wire this is nTerms 2 with
# M[1] = 0 and the node drifting; here M simply carries no T term, which
# is the same set. Under a rule keyed on the shared nTerms this body
# would be FROZEN at M = 10 and sit ~130 degrees away.
2415020.0, J2000, 10, 41, 0.02, 20, 30 + 1.0 * T, 1.5, CaseB
# C: a genuine M(T) -- the polynomial IS the mean anomaly of date, and no
# mean motion is added. The branch the other two do not exercise.
2415020.0, J2000, 10 + 400.0 * T, 41, 0.02, 20, 30, 1.5, CaseC
# D: the Earth origin, where n is divided by sqrt(M_sun/M_earth).
2451545.0, J2000, 0, 0.01, 0.05, 60, 40, 5, CaseD, geo
EOF

for o in sweph swephlib swemplan swemmoon swejpl swedate swehouse swecl; do
  [ -f "$o.o" ] || { echo "kind4-fixture: $o.o missing; run 'make astrolog' first" >&2; exit 1; }
done
g++ -O -I. -o "$out/kind4-fixture" tools/kind4-fixture.cpp \
  sweph.o swephlib.o swemplan.o swemmoon.o swejpl.o swedate.o swehouse.o swecl.o -lm

{
  echo "# kind-4 elements fixture: case, jd_tt, x, y, z (AU)"
  echo "# geometric, rectangular, mean ecliptic and equinox of J2000,"
  echo "# heliocentric for A-C and geocentric for D."
  "$out/kind4-fixture" "$out"
} > "$out/FIXTURE.tsv"
sha256sum "$out/FIXTURE.tsv" | awk '{print $1}' > "$out/FIXTURE.sha256"
cat "$out/FIXTURE.tsv"
echo "sha256: $(cat "$out/FIXTURE.sha256")"
