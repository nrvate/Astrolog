#!/bin/sh
# Assert that the CI workflow concluded success for a commit, before a
# release is built from it.
#
#   tools/ci-assert-green.sh <sha> [workflow-file] [repo]
#
# The release pipeline verifies a great deal about its own artifacts --
# every .deb and .rpm is installed into a clean container and asked for
# Chiron, the Windows package is unpacked and run, the macOS bundle is
# launched out of the mounted .dmg. What none of that covers is the
# program's own 3,812 assertions, the ten audits, or the behavioural
# differential, because those belong to ci.yml and a release does not
# re-run them.
#
# Which is fine as long as ci.yml actually passed on the commit being
# tagged. Nothing checked that. A tag on a red commit would produce
# packages that install perfectly and compute a correct Chiron while
# carrying whatever ci.yml had caught -- and the Chiron assertion is
# deliberately shallow, because its job is to prove the ephemeris was
# found rather than to re-test the program.
#
# gh needs GH_TOKEN in the environment, which every workflow already has.
set -e

sha=${1:?usage: ci-assert-green.sh <sha> [workflow-file] [repo]}
wf=${2:-ci.yml}
repo=${3:-${GITHUB_REPOSITORY:?set GITHUB_REPOSITORY or pass the repo}}

# WAIT for it, do not merely test it. Pushing a commit and its tag
# together starts ci.yml and release.yml at the same instant, so a gate
# that failed on "still running" would fail almost every release -- which
# is a foot-gun rather than a check. It failed exactly that way the first
# time it ran, on v8.00-qt.6.
#
# The bound is generous because ci.yml is thirteen jobs in about four
# minutes, and a release is not a thing anyone does in a hurry.
wait=${CI_GREEN_WAIT:-900}
waited=0
while :; do
  runs=$(gh run list --repo "$repo" --workflow "$wf" --commit "$sha" \
    --limit 20 --json status,conclusion,databaseId 2>/dev/null || echo '[]')
  n=$(printf '%s' "$runs" | grep -c '"databaseId"' || true)
  if [ "${n:-0}" -eq 0 ]; then
    echo "no $wf run found for $sha"
    echo "A release should be cut from a commit CI has actually seen."
    exit 1
  fi
  concl=$(printf '%s' "$runs" \
    | sed 's/},{/}\n{/g' | grep -o '"conclusion":"[^"]*"' \
    | head -1 | cut -d'"' -f4)
  case $concl in
    success)
      echo "CI green: $wf concluded success for $sha"
      [ "$waited" -gt 0 ] && echo "  (after waiting ${waited}s for it)"
      exit 0 ;;
    "")
      [ "$waited" -ge "$wait" ] && {
        echo "$wf still running for $sha after ${wait}s; giving up"; exit 1; }
      [ "$waited" -eq 0 ] && echo "$wf is still running for $sha; waiting"
      sleep 15
      waited=$((waited + 15)) ;;
    *)
      echo "$wf concluded '$concl' for $sha; refusing to release"
      exit 1 ;;
  esac
done
