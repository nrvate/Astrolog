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

# A -Yi directory the user typed goes into the ephemeris search path even
# if it does not exist. This is a regression test for a real bug: the path
# builder filtered every candidate through stat(), which is right for the
# directories Astrolog GUESSES at and wrong for the ones the user NAMED.
# On Windows stat() fails on a bare drive letter and, per MSVC's own
# documentation, on a trailing backslash -- so "-Yi1 M:\swe\" pointing at
# a real directory disappeared from the path with no message at all, and
# the "not found in PATH" diagnostic listed everything except the thing
# the user had asked for.
#
# A nonexistent directory is the right probe precisely because it is the
# case the filter used to remove. If it survives, an existing one does.
# It must also still be VISIBLE: the whole value of keeping it is that a
# mistyped path shows up in the diagnostic instead of vanishing.
echo
echo "== Ephemeris search path =="
probe=/nonexistent-astrolog-ephem-probe
out=`$QTENV "$BIN" -Yi1 "$probe" -qa 6 15 1990 12:00 0 122W19 47N36 -R1 _X 2>&1`
case $out in
  *"$probe"*)
    echo "  ok: a -Yi directory that does not exist is still searched" ;;
  *)
    echo "  FAIL: '-Yi1 $probe' never reached the ephemeris path."
    echo "        An explicit -Yi is an instruction, not a hint. A user"
    echo "        who mistypes one must see it in the diagnostic."
    exit 1 ;;
esac

# The entries are joined with ONE character. PATH_SEPARATOR is ";:" here --
# a character class Swiss hands to strchr(), its own comment reading
# "semicolon or colon may be used" -- so joining with the whole string
# works, and quietly spends two bytes of a 242-byte budget on every entry.
#
# This assertion lives on Linux specifically. The same check was written
# for tools/win-console-checks.sh first and PASSED under a sabotage that
# reintroduced the bug, because PATH_SEPARATOR is ";" alone on Windows and
# the broken and correct code emit the same character there. Here the two
# differ, so this can actually fail.
case $out in
  *';:'*)
    echo "  FAIL: ephemeris path entries are joined with ';:'."
    echo "        PATH_SEPARATOR is a character class, not a separator."
    exit 1 ;;
  *)
    echo "  ok: path entries are joined with a single character" ;;
esac
exit 0
