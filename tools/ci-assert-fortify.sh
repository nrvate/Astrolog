#!/bin/sh
# Assert a build still imports glibc's fortify checkers.
#
#   tools/ci-assert-fortify.sh astrolog 10
#
# QT_CI_PLAN.md item 3.4. This guards a net that already exists and that
# nothing else watches. CLAUDE.md records that work log item 142 "survived
# 17 gdb runs and a full ASan sweep because every hunt pointed at a build
# that structurally could not see it" -- the -O0 sanitizer build, where
# _FORTIFY_SOURCE is inactive. Measured: the ordinary builds import 10-12
# *_chk symbols, the ASan build imports 1.
#
# So the fortify class is covered wherever a normal binary is exercised.
# What is unguarded is that it stays true: adding -O0 for a debugging
# session, or a distribution changing its compiler defaults, removes the
# net with no diagnostic whatsoever, and the next item-142-shaped bug is
# again hunted in a build that cannot see it.
#
# WHAT THIS IS NOT. It asserts the mechanism is present, not that any
# particular overflow is caught. It guards a net; it is not itself one.
#
# Exit 0 when the binary imports at least the expected number.
set -eu

bin=${1:?usage: ci-assert-fortify.sh <binary> [minimum]}
want=${2:-10}
cd "$(dirname "$0")/.."

if [ ! -f "$bin" ]; then
  echo "NO FORTIFY CHECK: $bin does not exist."
  exit 1
fi

got=$(nm -D "$bin" | grep -c '_chk' || true)
if [ "$got" -lt "$want" ]; then
  echo "FORTIFY GONE: $bin imports $got *_chk symbols, expected at least $want."
  echo "The build lost _FORTIFY_SOURCE -- most likely -O0, or a changed"
  echo "compiler default. A fortify-detected overflow cannot fire in that"
  echo "build at all, and nothing else here would say so."
  exit 1
fi

echo "fortify live: $bin imports $got *_chk symbols (expected >= $want)"
