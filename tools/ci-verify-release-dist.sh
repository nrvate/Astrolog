#!/bin/sh
# Verify the set of artifacts a release is about to publish.
#
#   tools/ci-verify-release-dist.sh dist 9
#
# QT_CI_PLAN.md item 5.2. Two failures this prevents, both of which
# publish successfully and look fine:
#
#   - A silently-empty package job becomes a release with fewer binaries
#     than it claims. The count is EXACT, not a floor, for the same
#     reason every other count in this repository is: a floor tests the
#     guess, and "at least one artifact" passes a release missing five.
#
#   - A SHA256SUMS that covers part of what it ships. It verifies
#     perfectly under "sha256sum -c" and proves nothing -- the same
#     failure as the manifest check in tools/ci-verify-package.sh, and
#     the reason that one counts its own lines too.
#
# It lives here rather than in the workflow because a check that exists
# only inside YAML can be falsified only by cutting a release, which is
# the one thing nobody wants to do repeatedly to test a check.
set -eu

dir=${1:?usage: ci-verify-release-dist.sh <dir> <expected-count>}
want=${2:?usage: ci-verify-release-dist.sh <dir> <expected-count>}

[ -d "$dir" ] || { echo "NO DIST: $dir is not a directory."; exit 1; }

# The artifact kinds this project releases. ONE list, because the count
# below and the stray check further down are exact complements of each
# other, and keeping two copies of the list is precisely how they drifted:
# .dmg and .exe joined the release, only the count knew nothing about
# them, and v8.00-qt.3 failed to publish reporting "7" while printing all
# nine files it had just refused to count.
kinds='deb rpm zip dmg exe'

# Build the find expression once: -name *.deb -o -name *.rpm -o ...
set --; for k in $kinds; do set -- "$@" -o -name "*.$k"; done
shift   # drop the leading -o

got=$(find "$dir" -type f \( "$@" \) | wc -l)
if [ "$got" -ne "$want" ]; then
  echo "WRONG ARTIFACT COUNT: $got, expected exactly $want."
  echo "== what is there:"
  find "$dir" -type f | sed 's/^/  /'
  echo "== A release with fewer binaries than it claims is worse than no"
  echo "== release: it looks complete. Check which package job produced"
  echo "== nothing. If a new KIND of artifact was added, it needs to be"
  echo "== in the 'kinds' list at the top of this script."
  exit 1
fi

# Anything that is not an artifact should not be here, and would end up
# in the manifest and in the release.
stray=$(find "$dir" -type f ! -name SHA256SUMS ! \( "$@" \) | head -5)
[ -z "$stray" ] || {
  echo "STRAY FILES in $dir -- these would be published too:"
  echo "$stray" | sed 's/^/  /'; exit 1; }

( cd "$dir" && find . -type f ! -name SHA256SUMS -print0 | sort -z \
    | xargs -0 sha256sum > SHA256SUMS )
( cd "$dir" && sha256sum -c SHA256SUMS >/dev/null ) || {
  echo "SHA256SUMS does not verify against the files it names"; exit 1; }

lines=$(wc -l <"$dir/SHA256SUMS")
[ "$lines" -eq "$want" ] || {
  echo "MANIFEST INCOMPLETE: $lines lines for $want artifacts."; exit 1; }

echo "release dist ok: $want artifacts, all covered by SHA256SUMS"
cat "$dir/SHA256SUMS"
