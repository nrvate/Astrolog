#!/bin/sh
# Did the last nightly actually pass, and how old is it?
#
#   tools/ci-assert-nightly.sh [repo]
#
# Exists because of a failure this project had no way to notice. The
# external Swiss oracle was added to nightly.yml in b56f511, whose commit
# message says "The external oracle runs in CI after all". It never ran
# once: every nightly since failed that step with "no astrolog at
# ./astrolog", and the nightly was red for days while work continued
# against it. Nothing was watching, because "the nightly" is a web page
# somebody has to remember to open.
#
# ci-assert-green.sh answers "did ci.yml pass for THIS COMMIT", which is
# the pre-release gate. This answers the other question -- "is the slow
# lane healthy at all" -- which nothing asked.
#
# Two ways to fail, and the second is the quiet one:
#
#   - a job or step did not succeed
#   - the newest run is older than MAX_AGE_H hours, meaning the schedule
#     itself stopped firing. A workflow that no longer runs reports no
#     failures, which reads exactly like a workflow that passes.
set -eu

repo=${1:-${GITHUB_REPOSITORY:-nrvate/Astrolog}}
wf=${NIGHTLY_WORKFLOW:-nightly.yml}
maxage=${MAX_AGE_H:-30}

command -v gh >/dev/null || { echo "gh not found"; exit 2; }

# success or failure only. A CANCELLED run is the concurrency group doing
# its job -- a newer push superseded it -- and its jobs are cancelled
# rather than failed. Counting those as failures made the first version of
# this script report a perfectly healthy repository as broken, which is
# the fastest way to teach someone to ignore a check.
run=$(gh run list --repo "$repo" --workflow "$wf" --limit 20 \
        --json databaseId,status,conclusion,createdAt \
        -q '[.[]|select(.status=="completed")
             |select(.conclusion=="success" or .conclusion=="failure")][0]' \
        2>/dev/null || true)
[ -n "$run" ] && [ "$run" != "null" ] || {
  echo "no completed $wf run found in $repo"; exit 1; }

id=$(printf '%s' "$run" | sed -n 's/.*"databaseId":\([0-9]*\).*/\1/p')
when=$(printf '%s' "$run" | sed -n 's/.*"createdAt":"\([^"]*\)".*/\1/p')

echo "== $wf run $id, created $when"

# Age. date -d is GNU; on a Mac this is the BSD form, so try both rather
# than silently skipping the check that catches a dead schedule.
then_s=$(date -d "$when" +%s 2>/dev/null || date -j -f '%Y-%m-%dT%H:%M:%SZ' "$when" +%s 2>/dev/null || echo 0)
if [ "$then_s" -gt 0 ]; then
  age=$(( ( $(date +%s) - then_s ) / 3600 ))
  echo "   $age hours old"
  [ "$age" -le "$maxage" ] || {
    echo "STALE: the newest $wf run is ${age}h old, over the ${maxage}h bound."
    echo "== A schedule that stopped firing reports no failures, which"
    echo "== looks exactly like a schedule that passes."
    exit 1; }
fi

gh api "repos/$repo/actions/runs/$id/jobs" \
  -q '.jobs[]|"\(.conclusion // .status)\t\(.name)"' | sed 's/^/   /'

bad=$(gh api "repos/$repo/actions/runs/$id/jobs" \
  -q '.jobs[]|.name as $j|.steps[]
      |select(.conclusion!="success" and .conclusion!="skipped"
              and .conclusion!="cancelled")
      |"   \(.conclusion)  [\($j)] \(.name)"' || true)

[ -z "$bad" ] || {
  echo "NIGHTLY NOT CLEAN -- steps that did not pass:"
  printf '%s\n' "$bad"
  exit 1; }

echo "nightly clean: every step in run $id succeeded or was skipped"
