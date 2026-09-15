#!/bin/sh
# Build the AddressSanitizer binary and run the suite under it, in one
# command. This is the invocation CLAUDE.md spells out by hand; the script
# exists because every hand-rolled version of it (2026-09-14, the aspect
# list work) fumbled something in the chain -- a missing ASAN_OPTIONS, a
# log path typed twice, a run without the offscreen platform and stdin
# guards the runner supplies, or the suite built but never run.
#
#   tools/asan-suite.sh                    # whole suite, ~6-10 minutes
#   tools/asan-suite.sh aspect-sort        # one group, seconds
#   tools/asan-suite.sh "" 3600            # whole suite, longer watchdog
#
# The filter is the same substring match run-qt-tests.sh uses
# (ASTROLOG_QT_TESTS). The log lands where ci-run-suite.sh writes it and
# is echoed by it on failure. Exit status is the suite's, or 124 if the
# watchdog had to kill it -- the runner says which group it was in.
set -eu
cd "$(dirname "$0")/.."
filter=${1:-}
secs=${2:-2400}
log=${3:-/tmp/asan-suite-$$.log}

make qt-asan -j4
if [ -n "$filter" ]; then
  export ASTROLOG_QT_TESTS=$filter
fi
exec env QTTESTBIN=./astrolog-qt-asan ASAN_OPTIONS=detect_leaks=0 \
  tools/ci-run-suite.sh "$secs" "$log" -Yi1 ephem
