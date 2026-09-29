#!/bin/sh
# tools/system-header-check.sh -- the Qt builds find the VENDORED Swiss
# header, not one installed on the machine.
#
# ephsrv/ephswiss.h says #include "swephexp.h". The quote form looks in
# ephsrv/ first, then the -I paths, then the SYSTEM ones; without "-I ." the
# vendored ./swephexp.h is never reached, and a machine that has installed
# the thread-safe fork (/usr/local/include/swephexp.h) compiles against THAT
# header without a word while a fresh checkout has no such file and fails.
# The release dry run for v8.00-qt.25 was where anyone learned it: the
# include had been there since the modular-ephemeris work and the only
# machine building it was the one that hid it.
#
# It cannot be read out of the build's .d files: -MM and -MMD leave system
# headers OUT, so the wrong header is exactly the one that does not appear.
# Two checks instead:
#   1. every Qt makefile that adds "-I ephsrv" adds "-I ." on the same line;
#   2. a real -M preprocess (which does list system headers) of what the Qt
#      build includes, under those flags, resolves swephexp.h to ./swephexp.h.
#
#   tools/system-header-check.sh
#   tools/system-header-check.sh --selftest   # bad inputs it must refuse
set -u

# check_flags <makefile>...: 1 above.
check_flags() {
  rc=0
  for mk in "$@"; do
    bad=$(grep -nE '(^|[[:space:]])-I[[:space:]]*ephsrv([[:space:]]|$)' "$mk" | grep -vE '\-I[[:space:]]*\.([[:space:]]|$)')
    if [ -n "$bad" ]; then
      echo "system-header-check FAIL: $mk adds -I ephsrv without -I . :"
      echo "$bad" | cut -c1-140
      rc=1
    fi
  done
  return $rc
}

if [ "${1:-}" = "--selftest" ]; then
  d=$(mktemp -d) || exit 1
  printf 'CPPFLAGS = -DQT -I ephsrv $(QT_CFLAGS)\n' > "$d/bad.mk"
  printf 'CPPFLAGS = -DQT -I ephsrv -I . $(QT_CFLAGS)\n' > "$d/good.mk"
  check_flags "$d/bad.mk" >/dev/null 2>&1 && { echo "system-header-check SELFTEST FAIL: accepted -I ephsrv alone"; rm -rf "$d"; exit 1; }
  check_flags "$d/good.mk" >/dev/null 2>&1 || { echo "system-header-check SELFTEST FAIL: refused -I ephsrv -I ."; rm -rf "$d"; exit 1; }
  # The resolution half, with a decoy: a system-looking directory holding a
  # different swephexp.h must lose to ./swephexp.h when -I . is present and
  # win when it is not.
  mkdir -p "$d/sys" "$d/root/ephsrv"
  echo '/* decoy */' > "$d/sys/swephexp.h"; echo '/* vendored */' > "$d/root/swephexp.h"
  echo '#include "swephexp.h"' > "$d/root/ephsrv/x.h"; echo '#include "x.h"' > "$d/root/t.cpp"
  got=$(cd "$d/root" && g++ -M -isystem "$d/sys" -I ephsrv t.cpp | tr ' \\' '\n\n' | grep 'swephexp.h')
  case "$got" in "$d/sys/swephexp.h") ;; *) echo "system-header-check SELFTEST FAIL: decoy did not win without -I . ($got)"; rm -rf "$d"; exit 1;; esac
  got=$(cd "$d/root" && g++ -M -isystem "$d/sys" -I ephsrv -I . t.cpp | tr ' \\' '\n\n' | grep 'swephexp.h')
  case "$got" in "swephexp.h") ;; *) echo "system-header-check SELFTEST FAIL: vendored did not win with -I . ($got)"; rm -rf "$d"; exit 1;; esac
  rm -rf "$d"
  echo "system-header-check selftest clean"
  exit 0
fi

rc=0
check_flags Makefile.qt Makefile.qt.test Makefile.qt.asan Makefile.qt.ubsan || rc=1

QTC=$(pkg-config --cflags Qt5Widgets 2>/dev/null || pkg-config --cflags Qt6Widgets 2>/dev/null)
got=$(echo '#include "ephswiss.h"' | g++ -x c++ -std=gnu++17 -M -I ephsrv -I . $QTC - 2>/dev/null | tr ' \\' '\n\n' | grep -E 'swephexp\.h$' | sort -u)
if [ "$got" != "swephexp.h" ]; then
  echo "system-header-check FAIL: ephswiss.h resolves swephexp.h to: ${got:-nothing}"
  rc=1
fi
[ $rc -eq 0 ] || exit 1
exit 0
