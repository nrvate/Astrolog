#!/bin/sh
# Assert the compilers match the ones tools/warnings.txt was built with.
#
#   tools/ci-assert-toolchain.sh
#
# QT_CI_PLAN.md item 6.1. tools/warning_audit.py compares every -Wall
# diagnostic against a committed baseline, and a compiler's diagnostics
# are a property of its version: g++ 13 says things g++ 11 does not, in
# different words, about different lines. Run the audit on a different
# toolchain and it reports hundreds of differences, none of which are
# regressions -- which is worse than not running it, because the signal
# it exists to give is drowned.
#
# So the slow-lane job pins ubuntu-22.04, and this says so out loud rather
# than leaving the next person to work backwards from 300 mystery diffs.
# If the runner image moves, or the baseline is regenerated on something
# else, the two numbers here are the one place to change.
#
# It is a warning, not a failure, when only the minor version differs:
# 11.4.0 against 11.3.0 is very unlikely to move a diagnostic, and
# failing on it would make the job brittle for no gain.
#
# Exit 0 when both major versions match.
set -eu

want_gcc=${WANT_GCC:-11}
want_mingw=${WANT_MINGW:-10}

# mingw's -dumpversion answers "10-win32", not "10", so take the leading
# digits rather than splitting on a dot. Found by running this script: it
# reported a mismatch against the very toolchain the baseline was built
# with, which is the falsification catching the check.
ver() { "$1" -dumpversion 2>/dev/null | sed -n "s/^\([0-9][0-9]*\).*/\1/p"; }

got_gcc=$(ver g++ || true)
got_mingw=$(ver x86_64-w64-mingw32-g++ || true)

echo "g++            $(g++ --version 2>/dev/null | head -1)"
echo "mingw g++      $(x86_64-w64-mingw32-g++ --version 2>/dev/null | head -1)"

bad=0
if [ "$got_gcc" != "$want_gcc" ]; then
  echo "TOOLCHAIN MISMATCH: g++ major version is ${got_gcc:-absent}, baseline wants $want_gcc"
  bad=1
fi
if [ "$got_mingw" != "$want_mingw" ]; then
  echo "TOOLCHAIN MISMATCH: mingw g++ major version is ${got_mingw:-absent}, baseline wants $want_mingw"
  bad=1
fi

if [ "$bad" -ne 0 ]; then
  echo
  echo "tools/warnings.txt is a ledger of what THESE compilers say. Against"
  echo "another major version the warning audit reports differences that are"
  echo "not regressions, and the one signal it exists to give is lost."
  echo "Either pin the runner image back, or regenerate the baseline with"
  echo "tools/warning_audit.py --update and say in the commit which"
  echo "toolchain it now describes."
  exit 1
fi

echo "toolchain ok: g++ $want_gcc and mingw g++ $want_mingw, as the baseline expects"
