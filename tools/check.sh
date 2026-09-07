#!/bin/sh
# What CI used to run on every push, in one command, on this machine.
#
#   tools/check.sh          # FAST -- the pre-commit loop
#   make check              # the same thing
#   tools/check.sh full     # everything, for a release
#   make check-full
#
# TWO MODES, because one command cannot be both. The fast one is what you
# run after an edit: the generated tables, the audits, the three builds
# your change can actually break, and the suite. The full one adds the
# other two toolchains, a second run of the whole suite against Qt6, and
# the slower audits -- worth minutes before a release, not after every
# edit. Running the slow one by reflex is how a check stops being run.
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
#
# The mingw Win32 and Qt6 builds ARE here, when this machine has their
# toolchains, because each has silently stopped compiling before.
#   tools/swetest-oracle.sh           numbers against upstream Swiss
set -eu
cd "$(dirname "$0")/.."
mode=${1:-fast}
case $mode in
  fast|full) ;;
  *) echo "usage: check.sh [fast|full]"; exit 2 ;;
esac
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
gen  "settings fields"           settingsfields.h python3 tools/gen_settings_fields.py astrolog.h
for a in rc_audit rc_mnemonic_audit rc_field_audit rc_lookup_audit \
         rc_flagtype_audit rc_casttype_audit rc_context_audit \
         backend_parity_audit \
         defaults_audit registry_audit settings_coverage_audit \
         line_endings_audit fixture_coverage_audit qt_srcs_audit \
         vcxproj_audit; do
  step "$a" python3 "tools/$a.py"
done
step "build: console and Qt"     make -j4
step "build: the test binary"    make qt-test -j4

# Needs the console binary, so it goes after the build rather than up
# with the pure-Python audits. Eleven image writers, each checked against
# its own format instead of against a previous build -- the one question
# graphics-matrix.sh structurally cannot ask. Its --selftest corrupts a
# render of each and requires the complaint, so a checker that has
# stopped checking says so here rather than at the next release.
step "image_audit"               python3 tools/image_audit.py
step "image_audit --selftest"    python3 tools/image_audit.py --selftest

# Also needs the console binary, and it is here because nothing ran it.
# It used to live in the push differential, and there is no push lane any
# more -- so from the day that went, the one check that asks "does this
# option still MOVE a render" ran nowhere. A refactor then made
# SwissComputeStar() never return success: "-XG -XU" drew the same ink as
# "-XG" alone, no stars at all, and every "-XE" render sat until the
# harness timed it out. This audit names both in one run; the commit that
# broke it verified against the switch matrix, the chart matrix and the
# suite, none of which render graphics. Roughly ten seconds.
step "inert_option_audit"        python3 tools/inert_option_audit.py

# The "-W" argument counts, which are written out three times and which
# nothing compared. The Qt consumer and the Win32 one each had five
# spellings checked; NProcessSwitchesNullW() had none, and it is the one
# with no behaviour to notice a wrong count by. Both binaries, because
# they are two of the three implementations.
step "w-switch-arity (console)"  tools/w-switch-arity.sh ./astrolog
step "w-switch-arity (Qt)"       tools/w-switch-arity.sh ./astrolog-qt-test

# The other two toolchains, when this machine has them. Both are here
# because their absence has cost this project real time: Makefile.win
# went 62 commits without compiling while three work log items listed
# "Windows builds" among their nets, and the Qt6 build was an artifact
# somebody remembered making until CI kept it alive. CI does not any
# more, so this does -- when the toolchain is absent it says skipped
# rather than passing quietly.
if [ "$mode" = full ] && command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
  step "build: Win32 oracle (mingw)"  make win -j4
  step "build: Windows console"       make wcli -j4
elif [ "$mode" = full ]; then
  printf '%-34s %s\n' "build: Win32 oracle (mingw)" \
    "skipped -- no x86_64-w64-mingw32-g++"
fi
if [ "$mode" = full ] && \
   [ -d "${QT6_PKGCONFIG:-/usr/local/qt6/lib/pkgconfig}" ]; then
  step "build: Qt6"                   make qt6 -j4
  step "build: Qt6 test binary"       make qt6-test -j4
  # And RUN it. Building proves it compiles; the Qt6 binary is a
  # different Qt with different defaults -- rounding policy, font
  # database, the QFontDatabase statics -- and the suite is what says it
  # behaves. A CI job ran this until 2026-09-05; nothing did between
  # then and now.
  printf '%-34s ' "the suite, against Qt6"
  if QTTESTBIN=./astrolog-qt6-test \
       tools/ci-run-suite.sh 600 /tmp/check-suite-qt6.log -Yi1 ephem \
       >/tmp/check-qt6.out 2>&1; then
    grep -hoE '^PASS: .*' /tmp/check-suite-qt6.log | tail -1
  else
    echo FAILED; tail -20 /tmp/check-qt6.out | sed 's/^/    /'; fail=1
  fi
elif [ "$mode" = full ]; then
  printf '%-34s %s\n' "build: Qt6" "skipped -- no Qt6 outside pkg-config"
fi
if [ "$mode" = full ]; then
  step "the bundled ephemeris"   tools/check-ephem.sh
  step "the assertion scripts"   tools/ci-selftest.sh
fi
printf '%-34s ' "the suite"
if tools/ci-run-suite.sh 600 /tmp/check-suite.log \
     -Yi1 ephem >/tmp/check-suite.out 2>&1; then
  grep -hoE '^PASS: .*' /tmp/check-suite.log | tail -1
else
  echo FAILED; tail -20 /tmp/check-suite.out | sed 's/^/    /'; fail=1
fi
[ "$fail" -eq 0 ] || { echo "== something above failed"; exit 1; }
if [ "$mode" = fast ]; then
  echo "== all clear (fast; \"make check-full\" adds mingw, Qt6 and the"
  echo "   slower audits, and is what a release wants)"
else
  echo "== all clear"
fi
