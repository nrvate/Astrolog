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

# WHICH MATRICES. All four by default; the caller can ask for fewer.
#
#   tools/ci-differential.sh <base> <outdir> "chart influence graphics"
#
# They cover disjoint surfaces and none substitutes for another, so
# "fewer" is a statement about WHEN each runs, never about dropping one.
# The split exists because their costs differ by an order of magnitude and
# a gate that takes 24 minutes is a tax rather than a check:
#
#   chart      142 invocations   ~2 processes each
#   influence   48
#   graphics   448               ~2 processes each
#   switch    1058               ~5 each (astrolog, sed, head, grep, rm)
#
# switch-matrix.sh alone is roughly the process work of the other three
# several times over. Measured on a runner 2026-09-02: the chart matrix
# and both builds finished in 26 seconds and switch was still going when
# the job was cancelled at 25 minutes. Nothing was hung -- there is no
# network in the console build and no invocation blocks -- it is simply
# 1,058 invocations of five processes on a machine where a process costs
# a great deal more than it does on an NVMe workstation.
#
# So the pull-request gate runs the fast three, and the nightly lane runs
# all four. Coverage is unchanged over a day; feedback on a change is
# minutes rather than half an hour.
base=${1:?usage: ci-differential.sh <base-ref> [outdir] [matrices]}
out=${2:-out/diff}
want=${3:-chart switch influence graphics}
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
started=$(date +%s)

# The eight runs go in parallel, which is the difference between this job
# being a check and being a tax. Serially it was 24 minutes on a runner;
# the four matrices are independent and each invocation is single
# threaded, so they are pure fan-out on a multi-core runner.
#
# ONE EXCEPTION, and it is not negotiable: graphics-matrix.sh uses a
# FIXED temp directory, deliberately -- the PostScript writer embeds its
# output file name, so a random directory makes six renders differ
# between two runs of the same binary. Determinism is that harness's whole
# product. Its two runs therefore go one after the other inside their own
# background job, never side by side. The guard added to that script on
# 2026-09-02 turns a violation of this into a loud refusal rather than
# two corrupted baselines, but the right thing is not to violate it.
run_matrix() {   # $1 = name, runs base then head
  "tools/$1-matrix.sh" ./base-astrolog >"$out/$1.base" 2>&1 || return 1
  "tools/$1-matrix.sh" ./astrolog      >"$out/$1.head" 2>&1 || return 2
}
for m in $want; do
  case $m in
    graphics) ( run_matrix graphics ) & pid_graphics=$! ;;
    *) ( "tools/$m-matrix.sh" ./base-astrolog >"$out/$m.base" 2>&1 ) & eval "pid_${m}_b=$!"
       ( "tools/$m-matrix.sh" ./astrolog      >"$out/$m.head" 2>&1 ) & eval "pid_${m}_h=$!" ;;
  esac
done
for m in $want; do
  case $m in
    graphics) wait $pid_graphics || { echo "graphics: HARNESS FAILED -- see $out/graphics.*"; fail=1; } ;;
    *) eval "wait \$pid_${m}_b" || { echo "$m: HARNESS FAILED on the baseline -- see $out/$m.base"; fail=1; }
       eval "wait \$pid_${m}_h" || { echo "$m: HARNESS FAILED on HEAD -- see $out/$m.head"; fail=1; } ;;
  esac
done
el=$(( $(date +%s) - started ))

for m in $want; do
  printf '%-10s ' "$m"
  [ -s "$out/$m.base" ] && [ -s "$out/$m.head" ] || { echo "no output"; fail=1; continue; }
  nb=$(wc -l <"$out/$m.base"); nh=$(wc -l <"$out/$m.head")
  if diff -u "$out/$m.base" "$out/$m.head" >"$out/$m.diff" 2>&1; then
    echo "identical ($nh lines)"
    rm -f "$out/$m.diff"
  else
    nd=$(grep -c '^[-+][^-+]' "$out/$m.diff" || true)
    echo "MOVED: $nd lines of $nh -- $out/$m.diff"
    moved="$moved $m"
  fi
done

rm -f "$root/base-astrolog"
echo "== ${el}s for [$want ], in parallel"

if [ "$fail" -ne 0 ]; then
  echo "== a harness failed to run. A differential whose invocations all"
  echo "== error diffs to zero and reads exactly like a proof, so this is"
  echo "== a failure and not a clean result."
  exit 1
fi

[ -z "$moved" ] && { echo "== no behavioural movement in:$want"; exit 0; }

echo "== behaviour moved in:$moved"

# REPORT MODE, for the nightly. The per-change gate and a day's aggregate
# are different questions and only one of them should be able to fail.
#
# A gate on a pull request asks "what did THIS change move", and the
# author is right there to answer it. A nightly asks "what moved today",
# over everybody's commits at once -- so failing it punishes whoever
# pushed last for a change somebody else declared badly, and a nightly
# that is red for a legitimate reason is a nightly people stop reading.
# That is the cry-wolf failure, which is the same disease as a vacuous
# check caught from the other end.
#
# Measured on the first nightly run, 2026-09-02: it went red on 122 moved
# chart lines that were another session's house-degeneracy fix at extreme
# latitudes -- correct, intentional, and undeclared only because the
# trailer convention was a day old. Exactly the case this mode exists for.
if [ "${DIFFERENTIAL_REPORT:-}" = 1 ]; then
  echo "== report mode: the diffs are the output, not a verdict. The"
  echo "== per-change gate is what enforces; this says what moved today."
  exit 0
fi

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
