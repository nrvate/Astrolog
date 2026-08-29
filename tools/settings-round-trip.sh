#!/bin/sh
# Prove the settings file is a fixed point: save it, load what was saved,
# save again, and require the two saves to be byte-identical. The console
# build is the host on purpose -- no event loop, it exits, and it loads
# every switch family the GUIs write. "_X" is mandatory: this build IS the
# X11 build, and "=X" in a settings file opens a window and sits there
# (work log item 32). Exits nonzero on any drift.
#
#   make && tools/settings-round-trip.sh
set -e
cd "$(dirname "$0")/.." || exit 1
[ -x ./astrolog ] || { echo "build it first: make"; exit 1; }
T=${TMPDIR:-/tmp}
env -u DISPLAY ./astrolog -i nrvate.as _X -od "$T/rt-A.as" </dev/null >/dev/null 2>&1
env -u DISPLAY ./astrolog -i "$T/rt-A.as" _X -od "$T/rt-B.as" </dev/null >/dev/null 2>&1
if cmp -s "$T/rt-A.as" "$T/rt-B.as"; then
  echo "PASS: settings reach a fixed point after one round trip"
  rm -f "$T/rt-A.as" "$T/rt-B.as"
else
  echo "FAIL: a second save differs from the first:"
  diff "$T/rt-A.as" "$T/rt-B.as" | head -20
  exit 1
fi
