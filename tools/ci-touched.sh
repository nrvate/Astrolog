#!/bin/sh
# Does the range <base>..HEAD touch any of these paths?
#
#   tools/ci-touched.sh <base-sha> <path>...        # prints true or false
#
#   tools/ci-touched.sh HEAD~5 tools/astrolog.nsi .github/workflows/
#
# Prints "true" or "false" and exits 0 either way; a question, not an
# assertion. It exits nonzero only when it cannot answer.
#
# It answers "true" when it does not know: an empty or all-zero base (a
# new branch, or a pull_request event with no base recorded), a base that
# is not in this clone, or a git that refuses. A check skipped because
# the question could not be answered is the worst of the three outcomes,
# so the fallback is to do the work.
#
# Was used to decide whether a push needed the Windows zip and
# installer built and driven under Wine. That is release-grade work --
# about 94 s behind a 183 s build, on the fast lane's critical path --
# and the inputs it covers change a few times a month. A release always
# does it; a push does it when it touches the packaging.
set -eu
base=${1:?usage: ci-touched.sh <base-sha> <path>...}
shift
[ $# -ge 1 ] || { echo "usage: ci-touched.sh <base-sha> <path>..." >&2; exit 2; }
cd "$(dirname "$0")/.."

case $base in
  ""|0000000000000000000000000000000000000000)
    echo "true"; exit 0 ;;
esac
git rev-parse --verify --quiet "$base^{commit}" >/dev/null 2>&1 || { echo "true"; exit 0; }

if git diff --name-only "$base" HEAD -- "$@" 2>/dev/null | grep -q .; then
  echo "true"
else
  echo "false"
fi
