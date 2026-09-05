#!/bin/sh
# What CI used to run on every push, in one command, on this machine.
#
#   tools/check.sh          # about two minutes
#   make check              # the same thing
#
# CI became tag-only on 2026-09-05, so nothing runs these for you any
# more. This is the list, in the order that fails fastest: the generated
# tables, the audits, the builds, the suite, and the assertion scripts'
# own self-test. Run it before a commit.
#
# What it deliberately does NOT run, because each needs a baseline build,
# a container or minutes of sanitizer time -- they are in CLAUDE.md, and
# they are what a change to shared core still wants:
#
#   tools/ci-differential.sh <base>   four matrices against another commit
#   tools/build-check.sh              the source build on twelve distributions
#   tools/asan-sweep.sh, ubsan-sweep.sh, coverage-report.sh
#   tools/warning_audit.py            70 s, five builds, empty ledger
#   tools/swetest-oracle.sh           numbers against upstream Swiss
set -eu
cd "$(dirname "$0")/.."
fail=0
step() {
  name=$1; shift
  printf '%-34s ' "$name"
  if out=$("$@" 2>&1); then
    echo ok
  else
    echo FAILED
    printf '%s\n' "$out" | tail -12 | sed 's/^/    /'
    fail=1
  fi
}
gen() {   # a generated table still matches its generator
  name=$1; want=$2; shift 2
  printf '%-34s ' "$name"
  if "$@" 2>/dev/null | diff -q - "$want" >/dev/null; then echo ok
  else echo "FAILED -- regenerate: $* > $want"; fail=1; fi
}

gen  "dialogs from astrolog.rc"  qtrcdlg.h   python3 tools/rc2qt.py astrolog.rc
gen  "accelerators"              qtrcaccel.h python3 tools/rc_accel.py astrolog.rc
gen  "command ids"               qtrccmd.h   python3 tools/rc_cmd.py astrolog.rc resource.h
for a in rc_audit rc_mnemonic_audit rc_field_audit rc_lookup_audit \
         defaults_audit registry_audit line_endings_audit \
         fixture_coverage_audit qt_srcs_audit vcxproj_audit; do
  step "$a" python3 "tools/$a.py"
done
step "build: console and Qt"     make -j4
step "build: the test binary"    make qt-test -j4
step "inert options"             python3 tools/inert_option_audit.py
step "the assertion scripts"     tools/ci-selftest.sh
printf '%-34s ' "the suite"
if ASTROLOG_QT_EPHEM=minimal tools/ci-run-suite.sh 600 /tmp/check-suite.log \
     -Yi1 ephem >/tmp/check-suite.out 2>&1; then
  grep -hoE '^PASS: .*' /tmp/check-suite.log | tail -1
else
  echo FAILED; tail -20 /tmp/check-suite.out | sed 's/^/    /'; fail=1
fi
[ "$fail" -eq 0 ] || { echo "== something above failed"; exit 1; }
echo "== all clear"
