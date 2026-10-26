#!/bin/sh
# A PINNED, static zstd for the macOS job, the one library Ephemeris
# Prometheia needs (Makefile.prometheia; Prometheia's docs/EMBEDDING.md).
#
#   tools/ci-macos-zstd.sh <dir>        # e.g. "$HOME/zstd"
#
# Prints the environment Makefile.prometheia reads, in the form a workflow
# appends to $GITHUB_ENV: PROMETHEIA_ZSTD_CFLAGS and PROMETHEIA_ZSTD_LIBS.
#
# Why not Homebrew's. Homebrew builds its bottles for the runner's own
# macOS, so linking its libzstd -- dylib or archive -- into a program
# whose MACOSX_DEPLOYMENT_TARGET is 12.0 gives a binary that needs a newer
# macOS than the .dmg claims, or a dylib to ship and re-point inside the
# .app. Built here from the release tarball, with the job's deployment
# target, it is an archive linked into the binary: nothing to ship, and
# the same bytes from an unchanged commit.
#
# The tarball's SHA-256 is written below, not fetched: a checksum
# downloaded beside the file it checks proves only that the two came from
# the same place. It was read from the release's own .sha256 and from the
# file itself on 2026-10-25, and the two agreed.
#
# Runs on Linux too (without the deployment flag), which is how it was
# proven before any runner saw it.
set -eu
VER=1.5.7
SHA=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
dir=${1:?usage: tools/ci-macos-zstd.sh <dir>}
mkdir -p "$dir"
lib="$dir/zstd-$VER/lib"

if [ ! -f "$lib/libzstd.a" ] || [ ! -f "$lib/zstd.h" ]; then
  tgz="$dir/zstd-$VER.tar.gz"
  [ -f "$tgz" ] || curl -sS -L --fail -m 300 -o "$tgz" \
    "https://github.com/facebook/zstd/releases/download/v$VER/zstd-$VER.tar.gz"
  got=$( (shasum -a 256 "$tgz" 2>/dev/null || sha256sum "$tgz") | cut -d' ' -f1)
  [ "$got" = "$SHA" ] || { echo "zstd-$VER.tar.gz: SHA-256 $got, expected $SHA" >&2; exit 1; }
  tar -x -z -f "$tgz" -C "$dir"
  min=
  [ "$(uname -s)" != Darwin ] || min="-mmacosx-version-min=${MACOSX_DEPLOYMENT_TARGET:?set MACOSX_DEPLOYMENT_TARGET, or the archive claims the runner's macOS}"
  # The single-threaded archive: Prometheia decompresses its catalog
  # chunks one at a time and links no threads (EMBEDDING.md).
  make -C "$lib" -j4 libzstd.a CFLAGS="-O2 $min" >&2
fi
[ -f "$lib/libzstd.a" ] && [ -f "$lib/zstd.h" ] || { echo "no libzstd.a or zstd.h under $lib" >&2; exit 1; }
echo "== zstd $VER, static, at $lib" >&2

echo "PROMETHEIA_ZSTD_CFLAGS=-I$lib"
echo "PROMETHEIA_ZSTD_LIBS=$lib/libzstd.a"
