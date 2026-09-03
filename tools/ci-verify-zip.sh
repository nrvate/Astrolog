#!/bin/sh
# Verify the Windows .zip, not the directory it was made from.
#
#   tools/ci-verify-zip.sh out/package/astrolog-8.00-qt.3-windows.zip \
#                          out/package/astrolog-windows
#
# The .exe installer is installed, compared file-for-file and uninstalled
# under Wine before any release ships it. The .zip -- the other Windows
# artifact, and the one the release notes offer to anyone who would rather
# not run an installer -- was created with "zip -qr" and uploaded, and
# nothing ever opened it again.
#
# What that leaves unchecked is not exotic: a zip built from the wrong
# directory, one missing the ephemeris because a staging step reordered,
# or one whose top-level directory is not what the notes tell people to
# unpack. All three ship silently, because every check upstream of the
# zip looks at the STAGED TREE rather than at the archive.
set -eu

zipf=${1:?usage: ci-verify-zip.sh <zip> <staged-dir>}
staged=${2:?usage: ci-verify-zip.sh <zip> <staged-dir>}
[ -f "$zipf" ] || { echo "no such zip: $zipf"; exit 2; }
[ -d "$staged" ] || { echo "no such staged directory: $staged"; exit 2; }
command -v unzip >/dev/null || { echo "unzip not found"; exit 2; }

T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
unzip -qq "$zipf" -d "$T" || { echo "THE ZIP DOES NOT EXTRACT: $zipf"; exit 1; }

top=$(ls "$T")
[ "$(printf '%s\n' "$top" | wc -l)" = "1" ] || {
  echo "THE ZIP HAS NO SINGLE TOP-LEVEL DIRECTORY -- it would unpack as a"
  echo "mess into whatever directory the user is standing in:"
  printf '%s\n' "$top" | sed 's/^/  /'; exit 1; }
echo "== unpacks as a single directory: $top"

# Byte-for-byte against what was staged. "diff -r" rather than a file
# count: a count catches a missing file and misses a corrupted one.
if ! diff -r "$staged" "$T/$top" >"$T/diff.out" 2>&1; then
  echo "THE ZIP DOES NOT MATCH THE STAGED PAYLOAD:"
  head -12 "$T/diff.out" | sed 's/^/  /'; exit 1; fi
echo "   identical to the staged payload"

# And re-run the package audit against the EXTRACTED tree, so the
# ephemeris count and the required/forbidden lists are asserted about what
# a user actually unpacks rather than about a directory on the runner.
tools/ci-verify-package.sh "$T/$top"
