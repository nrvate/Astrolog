#!/bin/sh
# Assert a build product is actually new -- QT_CI_PLAN.md item 1.2.
#
#   tools/ci-assert-fresh.sh astrolog.exe
#
# CLAUDE.md: "if a check claims to have rebuilt something, look at the
# binary's timestamp." A build that fails while a stale binary sits on
# disk reads as a pass to everything downstream, and this project has
# already spent 62 commits in exactly that state -- Makefile.win had not
# compiled since 2026-08-29 while three work log items listed "Windows
# builds" among their nets.
#
# On a fresh runner this check is trivially true, which is the point. It
# costs nothing today and it fires the day someone adds caching or an
# incremental build and the guarantee quietly disappears.
#
# It is a script rather than a workflow step on purpose: a check that
# exists only inside YAML can only be falsified by pushing, which is
# minutes a try, and CLAUDE.md's standing rule is that a harness proves
# nothing until you sabotage it. Here that is
# "touch astrolog.cpp && tools/ci-assert-fresh.sh astrolog.exe", which
# takes a second.
#
# Exit 0 when the binary exists and no .cpp or .h is newer than it.
set -eu

bin=${1:?usage: ci-assert-fresh.sh <binary>}
cd "$(dirname "$0")/.."

if [ ! -f "$bin" ]; then
  echo "NOT FRESH: $bin does not exist -- the build step did not produce it."
  exit 1
fi

# Object directories hold the compiler's own .d files and nothing this
# cares about; .git holds nothing at all.
newer=$(find . -name .git -prune -o -name 'obj-*' -prune -o \
  \( -name '*.cpp' -o -name '*.h' \) -newer "$bin" -print | sort)

if [ -n "$newer" ]; then
  echo "NOT FRESH: $bin is older than the sources it is built from --"
  echo "$newer" | sed 's/^/  /'
  echo "The build either did not run or ran against a stale object tree."
  exit 1
fi

echo "fresh: $bin is newer than every .cpp and .h in the tree"
