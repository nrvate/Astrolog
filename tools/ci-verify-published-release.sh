#!/bin/sh
# Download a PUBLISHED release and verify it the way a user would.
#
#   tools/ci-verify-published-release.sh v8.00-qt.4 [repo]
#
# Everything else here checks artifacts BEFORE they are published:
# ci-verify-release-dist.sh counts them and writes the manifest,
# ci-verify-package.sh and ci-verify-zip.sh audit their contents,
# ci-verify-windows-installer.sh installs one under Wine. All of that
# looks at files on a runner.
#
# This is the other question, and it found a real defect the day it was
# first asked by hand. GitHub rewrites "~" to "." in a release asset's
# filename at UPLOAD time, so v8.00-qt.3 shipped a SHA256SUMS naming two
# .deb files the release did not serve; "sha256sum -c" reported FAILED on
# both, their bytes were perfect, and nothing in CI could see it because
# nothing in CI downloads what was published.
#
# Deliberately NOT a release gate: the assets do not exist until after the
# release job finishes, and a check that has to be retried until the CDN
# agrees is not a check. Run it after a release, or from the nightly
# against the newest tag.
set -eu

tag=${1:?usage: ci-verify-published-release.sh <tag> [repo]}
repo=${2:-${GITHUB_REPOSITORY:-nrvate/Astrolog}}

# Releases published BEFORE the rename fix, which all carry a manifest
# naming two .deb files GitHub does not serve. They are reported and
# skipped rather than failed, so this can run nightly against the newest
# tag without being red for a reason nobody is going to act on -- a check
# that is always red is a check people stop reading.
#
# The list is closed and will never grow: every release from v8.00-qt.4 on
# renames before hashing. If a tag NOT on this list fails, that is the
# fix having regressed, which is the whole point of running it.
case " v8.00-qt.1 v8.00-qt.2 v8.00-qt.3 " in
  *" $tag "*)
    echo "== $tag predates the SHA256SUMS rename fix"
    echo "   Its manifest names two .deb files GitHub does not serve, because"
    echo "   the '~' rewrite happens at upload and the manifest was written"
    echo "   before it. Known, recorded, and not repaired retroactively:"
    echo "   that would change what is publicly served."
    echo "skipped: $tag is a known pre-fix release"
    exit 0 ;;
esac
command -v gh >/dev/null || { echo "gh not found"; exit 2; }

# Two directories, not one. The first draft wrote its working lists into
# the same directory it had just downloaded the release into, so "ls" of
# the assets listed the lists as well and reported them as published files
# nothing named. A scratch area must not sit inside the thing being
# measured.
T=$(mktemp -d); W=$(mktemp -d); trap 'rm -rf "$T" "$W"' EXIT
echo "== downloading every asset of $tag from $repo"
gh release download "$tag" --repo "$repo" -D "$T" --clobber >/dev/null 2>&1 || {
  echo "could not download $tag -- does the release exist?"; exit 2; }

n=$(ls "$T" | wc -l | tr -d ' ')
echo "   $n files"
[ -f "$T/SHA256SUMS" ] || {
  echo "NO SHA256SUMS IN THE RELEASE. Nothing published can be verified."
  exit 1; }

# Every published asset must be named in the manifest, and every name in
# the manifest must be a published asset. Both directions: the first
# catches an artifact shipped outside the manifest, the second is what the
# "~" rewrite broke.
sed 's/^[0-9a-f]*  *//; s|^\./||' "$T/SHA256SUMS" | LC_ALL=C sort > "$W/named"
ls "$T" | grep -v '^SHA256SUMS$' | LC_ALL=C sort > "$W/present"
if ! diff -q "$W/named" "$W/present" >/dev/null 2>&1; then
  echo "THE MANIFEST DOES NOT DESCRIBE THE RELEASE:"
  diff "$W/named" "$W/present" | sed 's/^/  /'
  echo "== '<' is named in SHA256SUMS but not published;"
  echo "== '>' is published but not named. GitHub rewrites '~' to '.' in"
  echo "== asset filenames at upload time, which is how this happens."
  exit 1; fi
echo "   the manifest names exactly the published assets"

( cd "$T" && sha256sum -c SHA256SUMS >"$W/check.out" 2>&1 ) || {
  echo "CHECKSUM MISMATCH on a published asset:"
  grep -v ': OK$' "$W/check.out" | head -10 | sed 's/^/  /'; exit 1; }
echo "   every published asset matches its digest"
echo "published release ok: $tag, $((n-1)) artifacts, all verified as downloaded"
