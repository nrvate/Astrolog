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
T_SKIP=$(mktemp); trap 'rm -f "$T_SKIP"' EXIT

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
# The qt6 entry is the awkward one and is allowed for a stated reason
# rather than a good one: warning_audit.py cannot check the Qt6 warning
# ledger on the runner that checks the others. That job is pinned to
# ubuntu-22.04 because tools/warnings.txt is a ledger of what g++ 11 says,
# and that image's qt6-base-dev ships no pkg-config files at all --
# measured in the image. ubuntu-latest has the .pc files and g++ 13, so
# one runner cannot do both. It is filed as OPEN in QT_CI_PLAN.md, and it
# is allowlisted here only so that this check is not permanently red over
# a known constraint: a check that always fails is one people stop
# reading, which is the failure this whole scan exists to prevent. When
# that item is closed, DELETE this line -- an allowlist entry that
# outlives its reason hides the next skip behind it.
allowed_skip() {
  case $1 in
    *"qt6: skipped, no Qt6"*)      return 0 ;;  # see below -- a real open item
    *"skipped on purpose"*)        return 0 ;;  # menu parity: 12 Windows-only items
    *"Skipping plugin q"*)         return 0 ;;  # windeployqt, choosing backends
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

echo "nightly clean: every step in run $id succeeded, and none skipped"
echo "silently"
