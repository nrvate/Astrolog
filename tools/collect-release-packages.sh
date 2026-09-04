#!/bin/sh
# Download the Linux packages of every published release, and keep only
# the ones for distributions the matrix still builds.
#
#   tools/collect-release-packages.sh <dir> [repo]
#
# repo.yml used to do the first half inline and no half of the second,
# so the repository served every package ever published: Fedora 42's,
# months after that release stopped receiving updates and after the
# matrix had moved on. ci-verify-repo.sh reported those suites as
# "served, but no longer in the build matrix -- not verified", which is
# the honest description of a package nobody will test again. The
# maintainer's decision (2026-09-04) is that the site serves what the
# matrix builds, and nothing it does not.
#
# tools/prune-releases.sh handles the other axis -- how many RELEASES
# the site serves -- and runs when a release is cut. This one filters by
# DISTRIBUTION, and runs when the site is rebuilt. Between them the
# repository holds the newest two releases for the distributions
# currently built, which is exactly the set ci-verify-repo.sh can
# install, upgrade and verify in a clean container.
#
# What is dropped is printed by name. A filter that works in silence
# would be the next thing to hide a mistake behind a green job.
set -eu

dir=${1:?usage: collect-release-packages.sh <dir> [repo]}
repo=${2:-${GITHUB_REPOSITORY:-nrvate/Astrolog}}
command -v gh >/dev/null || { echo "gh not found"; exit 2; }

current=$(python3 tools/distros.py dists 2>/dev/null) || {
  echo "cannot determine the distribution list (tools/distros.py failed)"; exit 1; }
echo "== distributions the matrix builds: $current"

mkdir -p "$dir"
for tag in $(gh release list --repo "$repo" --json tagName --jq '.[].tagName'); do
  gh release download "$tag" --repo "$repo" -p '*.deb' -p '*.rpm' -D "$dir" --clobber || true
done

kept=0; dropped=0
for f in "$dir"/*.deb "$dir"/*.rpm; do
  [ -e "$f" ] || continue
  case $f in
    # astrolog_8.00+qt.6.noble_amd64.deb -- GitHub rewrote "~" to "." in
    # the asset name, so both spellings are allowed for.
    *.deb) dist=$(basename "$f" | sed -n 's/.*[~.]\([a-z][a-z]*\)_amd64\.deb$/\1/p') ;;
    *.rpm) dist=$(basename "$f" | sed -n 's/.*\.\(fc[0-9][0-9]*\|el[0-9][0-9]*\)\.x86_64\.rpm$/\1/p') ;;
  esac
  [ -n "$dist" ] || { echo "cannot read a distribution from $(basename "$f")"; exit 1; }
  case " $current " in
    *" $dist "*) kept=$((kept + 1)) ;;
    *) echo "   dropping $(basename "$f"): $dist is no longer built"
       rm -f "$f"; dropped=$((dropped + 1)) ;;
  esac
done
[ "$kept" -gt 0 ] || { echo "no package left for any current distribution -- refusing to publish nothing"; exit 1; }
echo "== $kept packages kept, $dropped dropped"
ls -l "$dir"
