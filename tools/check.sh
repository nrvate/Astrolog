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
# other two toolchains, a second run of the whole suite against Qt6, a
# third under the maintainer's own settings file, and the slower audits --
# worth minutes before a release, not after every edit. Running the slow
# one by reflex is how a check stops being run.
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
#                                     (the three Linux ones ARE here, cached)
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
# The suite's logs. CI uploads /tmp/check-suite.log when a release run
# fails, so a CI run keeps that path; anywhere else each run gets its own
# directory. Two checks at once -- two worktrees, two sessions on one
# machine -- used to share the file: one truncated it under the other,
# the log came back starting with 3274 zero bytes, grep called it binary,
# and a suite that had passed 5746 to 0 was reported as "printed no PASS
# line" (2026-09-16).
if [ -n "${CI:-}" ]; then
  SUITE_TMP=/tmp
else
  SUITE_TMP=$(mktemp -d /tmp/check.XXXXXX)
fi
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
gen  "ephemeris parameters"      ephparam.h python3 tools/gen-eph-params.py --stdout
# Appendix A's registries as JSON, for a second implementation to vendor:
# generated from the prose, and the codec test below requires ephproto.h's own
# constants to agree with the file, so prose, file and code cannot drift.
gen  "protocol v4 registries"    ephsrv/registries.json python3 tools/gen-registries.py --stdout
# The protocol v4 conformance fixtures (EPHEMERIS_PLUGINS_PLAN.md 3.10) are a
# directory, not one file, so the generator checks itself -- contents and the
# set's own checksum.
step "protocol v4 fixtures"      python3 tools/ephproto4-fixtures.py --check
step "protocol v4 codec"         sh -c 'make -s ephproto_test && ASAN_OPTIONS=detect_leaks=0 ./ephproto_test ephsrv/conformance'
# The protocol header is vendored and compiled by the other implementation,
# so it has to stand on its own: no Astrolog header, C++20, -Werror.
step "protocol header vendorable" tools/ci-assert-vendorable.sh
for a in rc_audit rc_mnemonic_audit rc_field_audit rc_lookup_audit \
         rc_flagtype_audit rc_casttype_audit rc_context_audit \
         backend_parity_audit \
         defaults_audit registry_audit settings_coverage_audit \
         line_endings_audit fixture_coverage_audit qt_srcs_audit \
         horizons_audit star_identity_audit vcxproj_audit; do
  step "$a" python3 "tools/$a.py"
done
step "build: console and Qt"     make -j4
step "build: the test binary"    make qt-test -j4

# And astrolog-ephd, WHERE THE THREAD-SAFE FORK IS THERE TO BUILD IT
# AGAINST. The suite's ephem-server-live group launches the real binary
# and compares its answers byte for byte against the local Swiss cast, and
# it decides whether to run by asking whether that binary EXISTS. Existing
# is not the same as being current, and a stale one is worse than a
# missing one: a missing one skips with a printed reason and the suite
# stays green, while a stale one runs. On 2026-09-18 a protocol-3 binary
# five weeks old sat in this tree and failed 37 assertions across
# animation, reconnection, the window cache and wss, none of which named
# the real cause -- and the other way round is worse still, since a stale
# binary that still answers grades new code against old behaviour and
# passes. "make" is the check: it is a no-op when the binary is current.
if [ -f /shares/swisseph/libswe.a ] || [ -d /shares/swisseph ]; then
  step "build: astrolog-ephd"    make ephsrv -j4
else
  echo "   skipped: build: astrolog-ephd (no /shares/swisseph to link)"
fi

# The compiler's warnings, for every source file the three Linux builds
# compile. The full audit is five builds and 70 s, so it is not here -- and
# that is how two commits in a row (028b4ba, dcd939b) landed six
# -Wunused-variable in qttest.cpp with this command green. What stood here
# next was a gate on qttest.cpp alone, which left the other sixty-odd files
# and every header to an audit nothing ran.
#
# --cached keeps the objects and what the compiler said about each outside
# the tree, one cache per checkout, so a run with nothing changed is under a
# second and an edit recompiles what make says it touches -- a header edit
# included. Cold, it is about a minute and a half at -j4.
#
# Console, qt and qt-test, not win, wcli or Qt6: a release runner has no
# mingw and no hand-installed Qt6. A subset run is not normally a gate (its
# first column renames shared sites); --gate-subset makes it one on "any
# warning at all", which holds because tools/warnings.txt is empty, and it
# refuses outright the day that stops being true. That is its exit code, not
# a test of its output: the output names what it found either way.
step "warnings: Linux builds"    python3 tools/warning_audit.py --cached \
                                   --gate-subset --build console \
                                   --build qt --build qt-test

# And the same two Qt builds through clang, the compiler the macOS release
# job uses, held to that job's rule: the makefiles' own flags and not one
# warning. GCC and clang disagree about what is worth saying, and the first
# run of the v8.00-qt.19 release failed on macOS for arrays sized by a
# pointer difference that g++ compiled in silence -- an hour after the tag,
# and on MSVC too, which rejects the same construct outright. This puts that
# class in front of the commit instead. A machine with no clang skips it;
# the macOS job stays the gate of record there.
step "clang: both Qt builds"     tools/clang-gate.sh

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

# A command line typed at the console prompt that is longer than the prompt
# reads is refused whole. InputString() used to leave the rest of the line in
# stdin, where the next read ran it as a second command; the post-build
# review's wider buffer moved that split rather than removing it.
step "long prompt line refused"  tools/long-prompt-check.sh ./astrolog

# An object with no .d beside it is rebuilt, since make cannot otherwise
# know its headers; a stale xdata.o broke animated GIFs (Makefile.qt).
step "objects without .d rebuilt"  tools/stale-object-check.sh

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
       tools/ci-run-suite.sh 600 $SUITE_TMP/check-suite-qt6.log -Yi1 ephem \
       >$SUITE_TMP/check-qt6.out 2>&1; then
    grep -hoE '^PASS: .*' $SUITE_TMP/check-suite-qt6.log | tail -1
  else
    echo FAILED; tail -20 $SUITE_TMP/check-qt6.out | sed 's/^/    /'; fail=1
  fi
elif [ "$mode" = full ]; then
  printf '%-34s %s\n' "build: Qt6" "skipped -- no Qt6 outside pkg-config"
fi
if [ "$mode" = full ]; then
  step "the bundled ephemeris"   tools/check-ephem.sh
  step "the assertion scripts"   tools/ci-selftest.sh
fi
printf '%-34s ' "the suite"
if tools/ci-run-suite.sh 600 $SUITE_TMP/check-suite.log \
     -Yi1 ephem >$SUITE_TMP/check-suite.out 2>&1; then
  grep -hoE '^PASS: .*' $SUITE_TMP/check-suite.log | tail -1
else
  echo FAILED; tail -20 $SUITE_TMP/check-suite.out | sed 's/^/    /'; fail=1
fi
# THE SAME SUITE UNDER THE MAINTAINER'S OWN SETTINGS, which is a DIFFERENT
# CONFIGURATION and was gated nowhere. The line above runs "-Yi1 ephem";
# QT_TESTING.md and this project's own documentation tell a person to run
# ./run-qt-tests.sh, which runs "-i nrvate.as". Only one of the two was ever
# checked, and the ungated one was failing.
#
# What was hiding in the gap: ERROR 9 (busy) treated as a dead window, so a
# cast needing more windows than the server holds unread fell back to local
# Swiss and raised a warning box. It needs five windows under nrvate.as and
# three under "-Yi1 ephem", so the gated configuration could not reach it.
# Fixed at 02bb758; this is what would have caught it.
#
# IN THE FULL LANE AND NOT THE FAST ONE, on measurement rather than taste:
# this run takes 149 s against the whole fast check's 92, because nrvate.as
# points at a 887,000-file ephemeris and the path lookups dominate. Trebling
# the pre-commit command is not worth it; a release paying 149 s is.
if [ "$mode" = full ]; then
  printf '%-34s ' "the suite, under nrvate.as"
  if [ ! -f nrvate.as ]; then
    echo "skipped -- no nrvate.as in this checkout"
  elif ! grep -q '^-Yi1 "/swe"' nrvate.as || [ ! -d /swe ]; then
    # Without the ephemeris nrvate.as names, every esoteric body reads
    # 0Ari00'00" and the run would fail for a reason that is not a defect.
    echo "skipped -- nrvate.as names an ephemeris this machine has not got"
  elif tools/ci-run-suite.sh 900 $SUITE_TMP/check-suite-nrv.log \
       -i nrvate.as >$SUITE_TMP/check-suite-nrv.out 2>&1; then
    grep -hoE '^PASS: .*' $SUITE_TMP/check-suite-nrv.log | tail -1
  else
    echo FAILED; tail -20 $SUITE_TMP/check-suite-nrv.out | sed 's/^/    /'
    fail=1
  fi
fi
[ "$fail" -eq 0 ] || { echo "== something above failed"; exit 1; }
if [ "$mode" = fast ]; then
  echo "== all clear (fast; \"make check-full\" adds mingw, Qt6 and the"
  echo "   slower audits, and is what a release wants)"
else
  echo "== all clear"
fi
