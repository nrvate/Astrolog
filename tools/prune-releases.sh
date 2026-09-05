#!/bin/sh
# Retire every GitHub release but the newest N.
#
#   tools/prune-releases.sh [keep] [--dry-run] [repo]
#
# The maintainer's decision, 2026-09-04: old releases are pruned. Two
# reasons, both measured that day:
#
#   - repo.yml rebuilds the apt/dnf repository from EVERY release, so a
#     package built for a distribution that has since left the matrix --
#     Fedora 42, end-of-life since May -- stays on the site forever,
#     served and never again verified. The repository checks reported such a
#     suite as "served, but no longer in the build matrix -- not
#     verified", which is honest and is not a state anyone wants.
#   - Six releases were cut in two days while CI was being built, and
#     the first three carry a SHA256SUMS naming two .deb files GitHub
#     does not serve. Nothing in them is worth keeping.
#
# "Newest N" and not "newer than a date", because a release is only
# superseded by another release. The default of 2 is the smallest number
# that kept the repository's upgrade check meaningful -- it installed
# the oldest version the repository serves and upgrades to the newest,
# and with one version there is nothing to upgrade from.
#
# THE GIT TAGS STAY. A release is a GitHub object holding assets; the tag
# is history, and deleting it would make "which commit was qt.3" a
# question for the reflog. The assets are what go, and that is not
# reversible: --dry-run says what would be deleted and deletes nothing.
#
# Run from release.yml's Publish job right after "gh release create", so
# that cutting a release is what retires the ones before it -- and
# repo.yml, which chains off the release, rebuilds the site from what is
# left. Runnable by hand with the same token gh already has.
set -eu

keep=2; dry=""; repo=${GITHUB_REPOSITORY:-nrvate/Astrolog}
for a in "$@"; do
  case $a in
    --dry-run) dry=1 ;;
    [0-9]*) keep=$a ;;
    *) repo=$a ;;
  esac
done
[ "$keep" -ge 1 ] || { echo "keep must be at least 1"; exit 2; }
command -v gh >/dev/null || { echo "gh not found"; exit 2; }

# Newest first, by creation time -- the order gh already returns, made
# explicit because "newest" is the whole decision. Drafts and
# prereleases are not published releases and are left alone.
all=$(gh release list --repo "$repo" --limit 100 \
        --json tagName,createdAt,isDraft,isPrerelease \
        --jq '[.[] | select(.isDraft|not) | select(.isPrerelease|not)]
              | sort_by(.createdAt) | reverse | .[] | "\(.tagName)\t\(.createdAt)"')
[ -n "$all" ] || { echo "no releases in $repo; nothing to prune"; exit 0; }

n=$(printf '%s\n' "$all" | wc -l | tr -d ' ')
echo "== $repo: $n releases, keeping the newest $keep"
printf '%s\n' "$all" | head -n "$keep" | sed 's/^/   keep    /'
old=$(printf '%s\n' "$all" | tail -n +$((keep + 1)) || true)
[ -n "$old" ] || { echo "   nothing older to prune"; exit 0; }
printf '%s\n' "$old" | sed 's/^/   delete  /'

[ -z "$dry" ] || { echo "== dry run: nothing deleted"; exit 0; }

printf '%s\n' "$old" | cut -f1 | while IFS= read -r tag; do
  gh release delete "$tag" --repo "$repo" --yes
  echo "   deleted $tag (tag kept)"
done
echo "== pruned to $keep"
