#!/bin/sh
# Diff this commit's behaviour against another commit's, over all four
# differential matrices.
#
#   tools/ci-differential.sh <base-ref> [outdir]
#   tools/ci-differential.sh HEAD~1
#
# QT_CI_PLAN.md Phase 7 -- the phase that makes CI strictly better than
# the local workflow rather than merely more reliable. QT_TESTING.md tells
# you to build a baseline by hand with "git worktree add"; CI already has
# the base commit, so the harness becomes cheaper to run than to skip.
#
# THE FOUR COVER DISJOINT SURFACES AND NONE SUBSTITUTES FOR ANOTHER. The
# switch matrix never renders a chart, so charts0-3.cpp, intrpret.cpp and
# the x*.cpp text paths are invisible to it; the chart matrix renders only
# text, so the drawing code is invisible to both and wants the graphics
# matrix. Work log item 143 changed 1,055 formatting calls, mostly in
# exactly that drawing code, and the switch matrix was byte-identical over
# 75,471 lines while proving nothing about them.
#
# THE BASELINE BINARY MUST SIT IN THE REPOSITORY ROOT. Astrolog builds its
# ephemeris search path from the executable's own directory, so a baseline
# anywhere else silently reads a different ephemeris -- or none -- and the
# differential then reports a numerical regression that is not there.
# Measured (item 7.2b): a baseline in /tmp produced 544 lines of plausible,
# alarming, entirely false movement, planet longitudes off by an arcminute,
# because it had fallen back to Moshier while HEAD read the Swiss files.
# .gitignore reserves /base-astrolog for exactly this.
#
# BOTH SIDES RUN AGAINST THE CURRENT TREE'S DATA. Same ephemeris, same
# astrolog.as, different code -- which is the comparison worth making. A
# change to a data file shows up as a difference in both columns and so
# cancels; that is a limitation to know about, not a bug.
#
# CONFIG LEVERS MUST MATCH ON BOTH SIDES, and they are set here rather
# than inherited, because two matrices take one and the output is not
# comparable across configurations.
#
# GATE, WITH AN EXPLICIT OPT-OUT (item 7.4, Q7). A differential answers
# "something changed", not "something broke", and CLAUDE.md is explicit
# that it "actively protects a wrong answer, because fixing a 30-year-old
# bug shows up as a regression". So a non-empty diff fails -- unless a
# commit between base and HEAD says the change was deliberate:
#
#   Behaviour-change: the -0q month range now rejects 0, as it always should have
#
# That keeps the default honest (a surprise is loud) while making an
# intended change one line of the commit message rather than a workflow
# edit.
set -eu

base=${1:?usage: ci-differential.sh <base-ref> [outdir]}
out=${2:-out/diff}
cd "$(dirname "$0")/.."
root=$(pwd)

# The configuration both sides run under. "-Yi1 ephem" is the only thing
# CI can have: /swe is a machine-local NAS mount of ~887,000 files.
GRAPHICS_MATRIX_CFG=${GRAPHICS_MATRIX_CFG:--Yi1 ephem}
INFLUENCE_MATRIX_CFG=${INFLUENCE_MATRIX_CFG:--Yi1 ephem}
export GRAPHICS_MATRIX_CFG INFLUENCE_MATRIX_CFG

if ! git rev-parse --verify --quiet "$base^{commit}" >/dev/null; then
  echo "NO BASELINE: '$base' is not a commit in this checkout."
  echo "In a workflow, actions/checkout clones at fetch-depth 1, so even"
  echo "HEAD~1 is absent unless fetch-depth is raised."
  exit 1
fi
basesha=$(git rev-parse --short "$base")
headsha=$(git rev-parse --short HEAD)
if [ "$(git rev-parse "$base")" = "$(git rev-parse HEAD)" ]; then
  echo "NOTHING TO DIFF: $base is HEAD."
  exit 1
fi

echo "== differential $basesha..$headsha"
mkdir -p "$out"

# The head binary. Built here rather than assumed, so the two sides are
# built the same way by the same make.
make -j4 >"$out/build-head.log" 2>&1 || {
  echo "HEAD BUILD FAILED -- see $out/build-head.log"; tail -20 "$out/build-head.log"; exit 1; }

# The baseline, built from a clean extraction so nothing uncommitted leaks
# into it, then moved to the repository root for the reason above.
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
git archive "$base" | tar -x -C "$tmp"
( cd "$tmp" && make -j4 ) >"$out/build-base.log" 2>&1 || {
  echo "BASE BUILD FAILED at $basesha -- see $out/build-base.log"
  tail -20 "$out/build-base.log"; exit 1; }
cp "$tmp/astrolog" "$root/base-astrolog"

fail=0
moved=""
# Each matrix is timed, and the elapsed seconds are printed whether it
# moved or not. Not decoration: the first real CI run took this job past
# 19 minutes for work that takes 1 m 42 s on the maintainer's box, and
# the log said only "in progress" because a step's output is not served
# until it ends. Eight matrix runs and two builds is the most expensive
# job in the file, and knowing WHICH of the eight is the slow one is the
# difference between tuning it and guessing at it.
started=$(date +%s)
for m in chart switch influence graphics; do
  t0=$(date +%s)
  printf '%-10s ' "$m"
  "tools/$m-matrix.sh" ./base-astrolog >"$out/$m.base" 2>&1 || {
    echo "HARNESS FAILED on the baseline -- see $out/$m.base"; fail=1; continue; }
  "tools/$m-matrix.sh" ./astrolog      >"$out/$m.head" 2>&1 || {
    echo "HARNESS FAILED on HEAD -- see $out/$m.head"; fail=1; continue; }
  nb=$(wc -l <"$out/$m.base"); nh=$(wc -l <"$out/$m.head")
  el=$(( $(date +%s) - t0 ))
  if diff -u "$out/$m.base" "$out/$m.head" >"$out/$m.diff" 2>&1; then
    echo "identical ($nh lines, ${el}s for both binaries)"
    rm -f "$out/$m.diff"
  else
    nd=$(grep -c '^[-+][^-+]' "$out/$m.diff" || true)
    echo "MOVED: $nd lines of $nh, ${el}s -- $out/$m.diff"
    moved="$moved $m"
  fi
done

rm -f "$root/base-astrolog"
echo "== $(( $(date +%s) - started ))s in the four matrices"

if [ "$fail" -ne 0 ]; then
  echo "== a harness failed to run. A differential whose invocations all"
  echo "== error diffs to zero and reads exactly like a proof, so this is"
  echo "== a failure and not a clean result."
  exit 1
fi

[ -z "$moved" ] && { echo "== no behavioural movement in any of the four"; exit 0; }

echo "== behaviour moved in:$moved"
if git log --format=%B "$base..HEAD" | grep -qi '^Behaviour-change:'; then
  echo "== declared by a Behaviour-change: trailer, so this is expected:"
  git log --format=%B "$base..HEAD" | grep -i '^Behaviour-change:' | sed 's/^/     /'
  exit 0
fi
echo "== and no commit between $basesha and $headsha declares it."
echo "== If the change is intended, say so in the commit message:"
echo "==   Behaviour-change: <one line on what moved and why>"
echo "== If it is not, the diffs above are the regression."
exit 1
