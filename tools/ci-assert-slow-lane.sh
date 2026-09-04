#!/bin/sh
# Did the last slow-lane run actually pass, and did every step of it do
# what it claims?
#
#   tools/ci-assert-slow-lane.sh [repo] [run-id]
#
# With no run id: the newest completed run of slow-lane.yml, which since
# 2026-09-04 means the newest by-hand dispatch. With one: that run, of
# any workflow -- and the one worth naming is a RELEASE run, because the
# slow lane's jobs now execute inside release.yml through workflow_call,
# so "did the lane pass on what shipped, and did every step of it do
# what it claims" is a question about the release run, not about any
# run of slow-lane.yml at all.
#
# Exists because of a failure this project had no way to notice. The
# external Swiss oracle was added to the slow lane in b56f511, whose
# commit message says "The external oracle runs in CI after all". It never
# ran once: every run since failed that step with "no astrolog at
# ./astrolog", and the lane was red for days while work continued
# against it. Nothing was watching, because a scheduled workflow is a web
# page somebody has to remember to open.
#
# Since 2026-09-04 the lane has no schedule: release.yml calls it and
# gates on it, so a red run blocks a release rather than sitting there.
# What this still answers is the part a job's conclusion cannot show --
# a step that SUCCEEDED while skipping the thing it exists to do -- and
# it reads the logs of passing jobs to find one. Run it after a release
# or a dispatch.
#
# ci-assert-green.sh answers "did ci.yml pass for THIS COMMIT", which is
# the pre-release gate. This answers "is the slow lane healthy at all".
#
# MAX_AGE_H, if set, also fails when the newest run is older than that
# many hours. It was the default when the lane was scheduled, because a
# schedule that stops firing reports no failures and reads exactly like
# one that passes. A lane that runs on releases has no expected cadence,
# so it is off unless asked for.
set -eu

repo=${1:-${GITHUB_REPOSITORY:-nrvate/Astrolog}}
wf=${SLOW_LANE_WORKFLOW:-slow-lane.yml}
maxage=${MAX_AGE_H:-}

command -v gh >/dev/null || { echo "gh not found"; exit 2; }
T_SKIP=$(mktemp); T_JOBS=$(mktemp)
trap 'rm -f "$T_SKIP" "$T_JOBS"' EXIT

# success or failure only. A CANCELLED run is the concurrency group doing
# its job -- a newer push superseded it -- and its jobs are cancelled
# rather than failed. Counting those as failures made the first version of
# this script report a perfectly healthy repository as broken, which is
# the fastest way to teach someone to ignore a check.
runid=${2:-${SLOW_LANE_RUN:-}}
if [ -n "$runid" ]; then
  run=$(gh run view "$runid" --repo "$repo" \
          --json databaseId,status,conclusion,createdAt,workflowName 2>/dev/null || true)
  [ -n "$run" ] || { echo "no run $runid in $repo"; exit 1; }
  wf=$(printf '%s' "$run" | sed -n 's/.*"workflowName":"\([^"]*\)".*/\1/p')
else
  run=$(gh run list --repo "$repo" --workflow "$wf" --limit 20 \
          --json databaseId,status,conclusion,createdAt \
          -q '[.[]|select(.status=="completed")
               |select(.conclusion=="success" or .conclusion=="failure")][0]' \
          2>/dev/null || true)
  [ -n "$run" ] && [ "$run" != "null" ] || {
    echo "no completed $wf run found in $repo"; exit 1; }
fi

id=$(printf '%s' "$run" | sed -n 's/.*"databaseId":\([0-9]*\).*/\1/p')
when=$(printf '%s' "$run" | sed -n 's/.*"createdAt":"\([^"]*\)".*/\1/p')

echo "== $wf run $id, created $when"

# Age. date -d is GNU; on a Mac this is the BSD form, so try both rather
# than silently skipping the check that catches a dead schedule.
then_s=$(date -d "$when" +%s 2>/dev/null || date -j -f '%Y-%m-%dT%H:%M:%SZ' "$when" +%s 2>/dev/null || echo 0)
if [ "$then_s" -gt 0 ] && [ -n "$maxage" ]; then
  age=$(( ( $(date +%s) - then_s ) / 3600 ))
  echo "   $age hours old"
  [ "$age" -le "$maxage" ] || {
    echo "STALE: the newest $wf run is ${age}h old, over the ${maxage}h bound."
    echo "== A schedule that stopped firing reports no failures, which"
    echo "== looks exactly like a schedule that passes."
    exit 1; }
fi

gh api "repos/$repo/actions/runs/$id/jobs" \
  -q '.jobs[]|"\(.conclusion // .status)\t\(.name)"' >"$T_JOBS"
sed 's/^/   /' "$T_JOBS"

# Is the Qt6 warning ledger checked by SOMETHING in this run?
#
# The warnings job skips its Qt6 leg because that runner has no Qt6, and
# the allowlist below tolerates that skip. It may only do so while some
# other job actually checks the ledger -- otherwise the allowlist is a
# standing promise that nothing keeps, which is precisely the failure
# this scan was written to catch, reintroduced one level up.
#
# So the tolerance is conditional. Delete the warnings-qt6 job and this
# goes back to failing on the skip, loudly, instead of quietly passing.
qt6job=$(awk -F'\t' '$2 ~ /Qt6 warning ledger/{print $1; exit}' "$T_JOBS")
qt6job=${qt6job:-absent}
echo "   (Qt6 ledger job: $qt6job)"

bad=$(gh api "repos/$repo/actions/runs/$id/jobs" \
  -q '.jobs[]|.name as $j|.steps[]
      |select(.conclusion!="success" and .conclusion!="skipped"
              and .conclusion!="cancelled")
      |"   \(.conclusion)  [\($j)] \(.name)"' || true)

[ -z "$bad" ] || {
  echo "SLOW LANE NOT CLEAN -- steps that did not pass:"
  printf '%s\n' "$bad"
  exit 1; }

# And now the part a conclusion cannot show: a step that SUCCEEDED while
# doing nothing.
#
# Three of these were found by hand on 2026-09-03, all behind green jobs.
# The Swiss oracle printed "no astrolog at ./astrolog" and failed, which
# was the visible one. The other two simply skipped: the Qt leg of
# ubsan-sweep.sh said "no Qt build available" because Qt was installed by
# a later step, and warning_audit.py said "no Qt6" because that job never
# installed one -- so the leg that had just found two real bugs, and the
# entire Qt6 warning ledger, contributed nothing while reporting success.
#
# A skip prints one line in the middle of a passing log, and nobody reads
# a passing log. This does.
#
# ALLOWED is annotated because an unexplained skip is the whole point. A
# pattern earns a line here by being a deliberate, documented skip; "it
# was already there" is not a reason.
#
# The qt6 entry is the awkward one, and it is CONDITIONAL rather than
# unconditional. warning_audit.py cannot check the Qt6 warning ledger on
# the runner that checks the others: that job is pinned to ubuntu-22.04
# because tools/warnings.txt is a ledger of what g++ 11 says, and that
# image's qt6-base-dev ships no pkg-config files at all -- measured in
# the image. ubuntu-latest has the .pc files and g++ 13. One runner
# cannot do both, so the leg skips there and always will.
#
# That skip was allowlisted flat for a while, which meant the ledger was
# verified by nothing and the allowlist said so in a comment nobody had
# to read. The warnings-qt6 job now checks it on a newer image, using
# --qt6-only: the Qt6 leg is a DIFFERENCE between each Qt6 build and its
# Qt5 twin in the same run, so a newer compiler subtracts out on both
# sides and only the Qt6-specific set remains.
#
# The skip is therefore tolerated only while that job is present and
# green -- see $qt6job above. An allowlist entry that outlives its reason
# hides the next skip behind it, so this one is wired to its reason
# instead of describing it.
allowed_skip() {
  case $1 in
    *"qt6: skipped, no Qt6"*)      # only while warnings-qt6 covers it
                                   [ "$qt6job" = success ] && return 0
                                   return 1 ;;
    *"skipped on purpose"*)        return 0 ;;  # menu parity: 12 Windows-only items
    *"Skipping plugin q"*)         return 0 ;;  # windeployqt, choosing backends
                                                 # (windows-qt.yml, if pointed there)
    *"sprintf("*|*"printf("*)      return 0 ;;  # a compiler quoting source, not a skip
    *"not available\", star_nr"*)  return 0 ;;  # ditto, swisseph's own message
  esac
  return 1
}

unexplained=""
for jid in $(gh api "repos/$repo/actions/runs/$id/jobs" -q '.jobs[].id'); do
  jname=$(gh api "repos/$repo/actions/jobs/$jid" -q '.name' 2>/dev/null)
  log=$(gh api "repos/$repo/actions/jobs/$jid/logs" 2>/dev/null | tr -d '\r')
  [ -n "$log" ] || continue
  # Lines the runner echoes back from the script itself start with the
  # ANSI command colour; they are the source of the check, not its output.
  printf '%s\n' "$log" \
    | grep -iE "skipped|skipping|no .* available|not available" \
    | grep -v '\[36;1m' \
    | while IFS= read -r line; do
        allowed_skip "$line" && continue
        printf '   [%s] %s\n' "$jname" "$(printf '%s' "$line" | sed 's/^[^ ]* //')"
      done
done > "$T_SKIP" 2>/dev/null || true
unexplained=$(cat "$T_SKIP" 2>/dev/null || true)
rm -f "$T_SKIP"

[ -z "$unexplained" ] || {
  echo "STEPS THAT PASSED WHILE SKIPPING SOMETHING:"
  printf '%s\n' "$unexplained"
  echo "== Each of these reported success. If the skip is deliberate, add"
  echo "== the pattern to allowed_skip() in this script WITH THE REASON."
  echo "== If it is not, something is not running that you think is."
  exit 1; }

echo "slow lane clean: every step in run $id succeeded, and none skipped"
echo "silently"
