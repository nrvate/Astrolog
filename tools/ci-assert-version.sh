#!/bin/sh
# Assert a release tag matches the version in the source.
#
#   tools/ci-assert-version.sh v8.00-qt.1
#   tools/ci-assert-version.sh            # just print what the source says
#
# QT_CI_PLAN.md item 5.1. swisseph's release job does this, for a reason
# worth keeping: if the tag and the source disagree, every bug report
# afterwards cites the wrong version, and nothing else in the pipeline
# will ever notice.
#
# The source of truth is szVersionCore and szVersionFork in astrolog.h.
set -eu

cd "$(dirname "$0")/.."
core=$(sed -n 's/^#define szVersionCore "\([^"]*\)".*/\1/p' astrolog.h | head -1)
fork=$(sed -n 's/^#define szVersionFork "\([^"]*\)".*/\1/p' astrolog.h | head -1)
[ -n "$core" ] || { echo "cannot read szVersionCore from astrolog.h"; exit 1; }
[ -n "$fork" ] || { echo "cannot read szVersionFork from astrolog.h"; exit 1; }

ver="$core-qt.$fork"
[ $# -ge 1 ] || { echo "$ver"; exit 0; }

want=${1#v}
if [ "$want" != "$ver" ]; then
  echo "TAG MISMATCH: tag says '$1', astrolog.h says '$ver'."
  echo "Either the tag is wrong, or szVersionFork was not bumped before"
  echo "tagging. Do not publish a release whose version is a guess."
  exit 1
fi
echo "version ok: tag $1 matches astrolog.h ($ver)"
