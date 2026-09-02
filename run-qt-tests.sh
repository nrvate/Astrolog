#!/bin/sh
# Run the Qt GUI test suite with no X display needed.
#
# QT_QPA_PLATFORM=offscreen renders into memory instead of onto a screen.
# QT_QPA_PLATFORMTHEME must be cleared as well: the GTK platform theme
# plugin opens a display of its own and aborts the process without one,
# whatever the platform plugin is set to.
#
# Exits non-zero if any check fails, so it can gate a commit or CI.
cd "$(dirname "$0")" || exit 1
# Which binary to drive. The default is the one "make qt-test" builds;
# the override exists for the Qt6 build ("make qt6-test" leaves
# ./astrolog-qt6-test), which is the same sources against a different
# Qt and wants this same suite run against it.
BIN=${QTTESTBIN:-./astrolog-qt-test}
[ -x "$BIN" ] || { echo "build it first: make qt-test"; exit 1; }

QTENV="env -u DISPLAY QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME="

# Default to the maintainer's settings file, because running without it is
# not a milder test, it is a quieter one. nrvate.as carries -Yi1 "/swe",
# and SwissEnsurePath() caches the ephemeris search path the first time it
# is asked -- so without it every esoteric body resolves to "???" and the
# checks that compare those names against the ephemeris skip themselves
# rather than fail. That was 20 assertions and 20 of 39 bodies quietly not
# tested, in the exact command CLAUDE.md gives as the pre-commit check,
# while CLAUDE.md's own hard rule says always to pass this file. Any
# argument given here still overrides it.
[ $# -gt 0 ] || set -- -i nrvate.as

$QTENV "$BIN" "$@"
rc=$?
[ $rc -eq 0 ] || exit $rc

# Startup diagnostics, which the in-process suite cannot reach.
#
# main() parses astrolog.as and then the command line long before Action()
# gets to InteractQt() and BeginQt() constructs the QApplication, so a
# warning raised from either used to build a QWidget with no application
# alive -- which Qt answers with qFatal() and a core dump. A mistyped file
# name on the command line took the process down instead of printing
# "File not found". TestBadInputQt() cannot catch this: it runs inside the
# program, after the QApplication exists, so it exercises the wrong path.
# These have to be separate processes.
echo
echo "== Startup diagnostics =="
fail=0
for arg in "-i /nonexistent-astrolog-test-file.as" "-t"; do
  out=`$QTENV "$BIN" $arg 2>&1`
  rc=$?
  case $out in
    *"Must construct a QApplication"*)
      echo "  FAIL: '$arg' built a QWidget before the QApplication"; fail=1 ;;
    *)
      if [ $rc -ge 128 ]; then
        echo "  FAIL: '$arg' died on signal `expr $rc - 128`"; fail=1
      else
        echo "  ok: '$arg' reported and exited $rc"
      fi ;;
  esac
done
[ $fail -eq 0 ] || { echo "FAIL: startup diagnostics"; exit 1; }
echo "  a startup warning reaches stderr instead of aborting"
exit 0
