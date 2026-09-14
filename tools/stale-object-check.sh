#!/bin/sh
# An object with no .d file beside it must be rebuilt, whatever its
# timestamp: without the .d, make does not know which headers it includes,
# so a header change left it stale. Makefile.qt carries the guard and the
# story (a stale xdata.o defined the old GI struct and animated GIFs broke).
#
# Run after the builds. For each makefile, take one built object, check
# make does not already want to rebuild it, hide its .d, and require that
# make now does. The .d is put back with its timestamp intact.
set -u

fail() { echo "FAIL: $*"; exit 1; }

check() {
  mk=$1; target=$2; obj=$3
  d=${obj%.o}.d
  [ -f "$obj" ] || fail "$obj is not built; build before running this"
  [ -f "$d" ] || fail "$d is already missing, so this run proves nothing"
  if make -n -f "$mk" $target 2>/dev/null | grep -q -- "-o $obj "; then
    fail "$mk already wants to rebuild $obj; build first, or the test is blind"
  fi
  mv "$d" "$d.stale-check"
  if make -n -f "$mk" $target 2>/dev/null | grep -q -- "-o $obj "; then
    rebuilt=1
  else
    rebuilt=0
  fi
  mv "$d.stale-check" "$d"
  [ "$rebuilt" = 1 ] || fail "$mk does not rebuild $obj when its .d is missing"
  echo "ok: $mk rebuilds $obj when its .d is missing"
}

check Makefile astrolog xdata.o
check Makefile.qt "" obj-qt/xdata.o
check Makefile.qt.test "" obj-qt-test/xdata.o
