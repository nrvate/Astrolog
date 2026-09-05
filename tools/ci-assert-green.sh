#!/bin/sh
# Assert that the CI workflow concluded success for a commit, before a
# release is built from it.
#
#   tools/ci-assert-green.sh <sha> [workflow-file] [repo]
#
# Written when a push lane existed and a release did not re-run its
# checks: a tag on a red commit would have produced artifacts that
# install and compute perfectly while carrying whatever that lane had
# caught. Since 2026-09-05 the release runs the suite itself, and the
# only runs on a commit are release runs -- so this is now the way to
# wait for one, and the default workflow below says so.
#
# The checks a release still does not run are the differential, the
# sanitizer sweeps and the warning audit; "make check" is the local
# stand-in, and CLAUDE.md lists the rest.
#
# gh needs GH_TOKEN in the environment, which every workflow already has.
set -e

sha=${1:?usage: ci-assert-green.sh <sha> [workflow-file] [repo]}
wf=${2:-release.yml}
repo=${3:-${GITHUB_REPOSITORY:?set GITHUB_REPOSITORY or pass the repo}}

# WAIT for it, do not merely test it. Pushing a commit and its tag
# together used to start two workflows at the same instant, so a gate
# that failed on "still running" would fail almost every release -- which
# is a foot-gun rather than a check. It failed exactly that way the first
# time it ran, on v8.00-qt.6.
#
# The bound is generous because a release run builds on three platforms,
# and a release is not a thing anyone does in a hurry.
wait=${CI_GREEN_WAIT:-900}
waited=0
while :; do
  runs=$(gh run list --repo "$repo" --workflow "$wf" --commit "$sha" \
    --limit 20 --json status,conclusion,databaseId 2>/dev/null || echo '[]')
  n=$(printf '%s' "$runs" | grep -c '"databaseId"' || true)
  if [ "${n:-0}" -eq 0 ]; then
    # A run takes a while to APPEAR after a push -- GitHub queues it,
    # and "gh run list --commit" is empty until then. Started seconds
    # after "git push" on 2026-09-05, this found nothing, said CI had
    # never seen the commit, and gave up while the run was being
    # created. So give a run time to appear before deciding it never
    # will; a commit CI has really never seen still fails, just later.
    if [ "$waited" -lt "${CI_GREEN_APPEAR:-180}" ]; then
      [ "$waited" -eq 0 ] && echo "no $wf run for $sha yet; waiting for one to appear"
      sleep 15
      waited=$((waited + 15))
      continue
    fi
    echo "no $wf run found for $sha after ${waited}s"
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
