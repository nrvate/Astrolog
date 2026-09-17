#!/bin/bash
# tools/ephsrv-fork.sh -- fetch and build the pinned Swiss Ephemeris fork.
#
#   tools/ephsrv-fork.sh DEST            # then: make ephsrv SWE_HOME=DEST
#
# astrolog-ephd links the thread-safe fork's libswe.a, and the fork is a
# sibling repository, not vendored. On the maintainer's machine it is
# /shares/swisseph, Makefile.ephsrv's default; anywhere else -- a container,
# a VPS -- this fetches exactly the commit in ephsrv/deploy/SWISSEPH_PIN and
# builds its library, so the server is the same one the gates passed.
#
# A shallow, blobless fetch of that one commit, checked out without its
# ephe/ directory -- the blobs of what is checked out are fetched on demand:
# 227 MB of data files the library build never reads (the server gets its
# ephemeris from --ephe). Refuses a DEST that holds anything but a previous
# run of this script, and a checkout whose HEAD is not the pin.
set -euo pipefail
cd "$(dirname "$0")/.."
DEST=${1:?usage: tools/ephsrv-fork.sh DEST}
# shellcheck disable=SC1091
. ephsrv/deploy/SWISSEPH_PIN
: "${SWE_REPO:?}" "${SWE_COMMIT:?}"

if [ -e "$DEST" ] && [ ! -d "$DEST/.git" ]; then
  echo "ephsrv-fork: $DEST exists and is not a checkout; refusing" >&2
  exit 1
fi
mkdir -p "$DEST"
cd "$DEST"
[ -d .git ] || git init -q
git remote remove origin 2>/dev/null || true
git remote add origin "$SWE_REPO"
git sparse-checkout set --no-cone '/*' '!/ephe/'
git fetch -q --depth 1 --filter=blob:none origin "$SWE_COMMIT"
git checkout -q --force FETCH_HEAD
[ "$(git rev-parse HEAD)" = "$SWE_COMMIT" ] ||
  { echo "ephsrv-fork: HEAD is $(git rev-parse HEAD), not the pin" >&2; exit 1; }
# From clean: objects of another commit must not be archived with these.
rm -f ./*.o ./*.d libswe.a
make -s -j"${JOBS:-4}" libswe.a
echo "ephsrv-fork: $(sed -n 's/^#define SE_VERSION[[:space:]]*"\([^"]*\)".*/\1/p' sweph.h) at ${SWE_COMMIT:0:12} built in $PWD"
