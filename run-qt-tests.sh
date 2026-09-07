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
# The startup probes below run the binary as its own process and read its
# output through command substitution. That waits for the PIPE, not for
# the process, so one that writes its answer and then fails to exit is
# left running while this script reports "ok" and moves on -- and they
# accumulate, one per suite run. REFACTORING.md lists "kill orphaned
# astrolog-qt processes left by interrupted suite runs" as a standing
# house habit, which is the symptom of exactly this.
#
# A watchdog turns that silent leak into a bounded wait. 60s is far above
# what any probe needs (each is one chart) and far below anything a person
# would sit through.
#
# AND IT HAS TO EXIST. macOS ships no "timeout" -- GNU coreutils installs
# it as "gtimeout" there, and Homebrew's coreutils is not on a runner by
# default. Every probe below then died with 127, "command not found", and
# 127 is nonzero: the two checks that ask only for a nonzero exit printed
# "ok" for a binary that never ran, and the release job went green on
# macOS for as long as no probe needed a real answer. The first one that
# did -- the restriction-recall probe added 2026-09-07 -- failed there and
# nowhere else, which is what exposed the rest.
QTTIMEOUT=
for _t in timeout gtimeout; do
  command -v "$_t" >/dev/null 2>&1 && { QTTIMEOUT="$_t 60"; break; }
done
QTRUN="$QTTIMEOUT $QTENV"

# Prove the runner can run the binary before trusting anything it says
# about one. This is the cheap half of the lesson above: a probe that
# never executed cannot fail, and a section of them reads exactly like a
# section that passed.
if ! $QTRUN "$BIN" -Yi1 ephem -qa 6 15 1990 12:00 0 122W19 47N36 -R1 _X \
     </dev/null >/dev/null 2>&1; then
  echo "the startup probes cannot run '$BIN' -- refusing to report on it"
  [ -n "$QTTIMEOUT" ] || echo "  (no timeout(1) or gtimeout(1) on PATH)"
  exit 1
fi
[ -n "$QTTIMEOUT" ] || echo "note: no timeout(1); startup probes run unbounded"

# And every probe reads from /dev/null, which is the other half of the
# same leak. Astrolog prompts for chart info it was not given, and these
# probes hand it deliberately broken input -- so one of them asking a
# question is not a hypothetical. With stdin inherited from whatever
# started this script, a question means a read on a live terminal or
# socket that never answers, and the probe sits there until the watchdog
# above fires. Measured: the "-Yi1 <nonexistent>" probe blocked in
# unix_stream_data_wait with stdin as a socket, and finished instantly
# with "</dev/null". The "Pager with no reader" section already did this,
# for its own reasons; every other probe wants it too.
QTIN=/dev/null

# Default to the maintainer's settings file, because running without it is
# not a milder test, it is a quieter one. nrvate.as carries -Yi1 "/swe",
# and SwissEnsurePath() caches the ephemeris search path the first time it
# is asked -- so without it every esoteric body resolves to "???" and the
# checks that compare those names against the ephemeris skip themselves
# rather than fail. That was 20 assertions and 20 of 39 bodies quietly not
# tested, in the exact command CLAUDE.md gives as the pre-commit check,
# while CLAUDE.md's own hard rule says always to pass this file. Any
# argument given here still overrides it. (Since 2026-09-03 the bundled
# ephem/ resolves the same 39 bodies, so "-Yi1 ephem" runs the suite at
# the same strength; the default stays because it is the maintainer's
# configuration and what the hard rule says to test with.)
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
  out=`$QTRUN "$BIN" $arg <"$QTIN" 2>&1`
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

# Paging plus a closed stdin must not eat the stack.
#
# Terminate() prints "Astrolog exited" through PrintSz(); PrintSz() stops
# to page once -YQ rows have gone by; paging asks InputString() to read a
# line; and InputString() calls Terminate() when stdin is at end of file.
# Nothing broke that cycle, so "-YQ 24" with the output piped -- which is
# what astrolog.as's own comment suggests setting, and what every script
# and every harness in this repository does -- recursed about 24,900
# frames deep and died on SIGSEGV.
#
# It hid for so long because this tree's astrolog.as ships "-YQ 0", so no
# harness here had the pager on. The assertion is only that the process
# does not die on a signal: exiting 2 is correct and documented, since an
# end-of-file on stdin is how Control+d is delivered.
#
# In-process tests cannot see this. The stack is gone by the time anything
# could report on it.
echo
echo "== Pager with no reader =="
fail=0
for q in 24 1; do
  $QTRUN "$BIN" -YQ $q -Yi1 ephem -qa 6 15 1990 12:00 0 122W19 47N36 \
    -R1 _X </dev/null >/dev/null 2>&1
  rc=$?
  if [ $rc -ge 128 ]; then
    echo "  FAIL: '-YQ $q' with stdin closed died on signal `expr $rc - 128`"
    echo "        Terminate() -> PrintSz() -> InputString() -> Terminate()"
    echo "        has lost its base case again."
    fail=1
  else
    echo "  ok: '-YQ $q' with stdin closed exited $rc"
  fi
done
[ $fail -eq 0 ] || { echo "FAIL: pager recursion"; exit 1; }

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
# The Object Restrictions dialog has a "Recall" button, and what it hands
# back is the restrictions InitRestrictions(fTrue) last stored. Windows
# stores that after reading astrolog.as and the command line; this build
# only had InitProgram()'s store, from before either was read, so Recall
# handed back the COMPILED defaults and discarded whatever the settings
# file restricted.
#
# In a plain run the two sets agree either way, because astrolog.as
# restricts nothing the defaults do not. "-R Sun" restricts the Sun on
# the command line, where the compiled default leaves it unrestricted --
# so the remembered set can only agree if the store really did happen
# after the switches. In process, after its own event loop is up, the
# suite cannot see this: by then startup is over.
echo
echo "== Restriction recall at startup =="
out=`ASTROLOG_QT_RECALL_PROBE=1 ASTROLOG_QT_TESTS=restrict-recall \
  $QTRUN "$BIN" -Yi1 ephem -R Sun <"$QTIN" 2>&1`
case $out in
  *"0 failed"*)
    echo "  ok: a command line restriction reaches the remembered set" ;;
  *)
    echo "  FAIL: the restrictions Recall hands back are not the ones"
    echo "        startup ended with. InitRestrictions(fTrue) has to run"
    echo "        after the switches, as it does in wdriver.cpp."
    echo "$out" | sed 's/^/        /'
    exit 1 ;;
esac

echo
echo "== Ephemeris search path =="
probe=/nonexistent-astrolog-ephem-probe
out=`$QTRUN "$BIN" -Yi1 "$probe" -qa 6 15 1990 12:00 0 122W19 47N36 -R1 _X \
  <"$QTIN" 2>&1`
# Both assertions read Swiss's own "not found in PATH" line, which is the
# only place the assembled path is observable from outside the process. If
# that line is absent the run proves nothing either way, so say so rather
# than blaming the thing being measured -- this runs on macOS too, where
# nobody working on this can reproduce a failure by hand.
case $out in
  *"not found in PATH"*) ;;
  *)
    echo "  FAIL: no \"not found in PATH\" line to read the path from."
    echo "        The probe cannot see anything; this is a broken check,"
    echo "        not a failed assertion. Run the command by hand:"
    echo "          $BIN -Yi1 $probe -qa 6 15 1990 12:00 0 122W19 47N36 -R1 _X"
    exit 1 ;;
esac
case $out in
  *"$probe"*)
    echo "  FAIL: '-Yi1 $probe' was handed to Swiss Ephemeris."
    echo "        It holds no ephemeris. Swiss keeps the whole path in a"
    echo "        256-byte buffer and DISCARDS ALL OF IT when it does not"
    echo "        fit, so every entry that cannot match costs a real one."
    exit 1 ;;
  *)
    echo "  ok: a -Yi holding no ephemeris is not handed to Swiss" ;;
esac

# The other half, and the one that is easy to lose while fixing the first:
# a -Yi that DOES hold an ephemeris must reach Swiss. Without this, a
# resolver that simply dropped every -Yi would pass the check above.
out2=`$QTRUN "$BIN" -Yi1 ephem -qa 6 15 1990 12:00 0 122W19 47N36 -R1 _X \
  <"$QTIN" 2>&1`
case $out2 in
  *"not found in PATH"*) ;;
  *)
    echo "  FAIL: no \"not found in PATH\" line in the -Yi1 ephem run."
    echo "        Broken check, not a failed assertion."
    exit 1 ;;
esac
case $out2 in
  *ephem*)
    echo "  ok: a -Yi holding an ephemeris is handed to Swiss" ;;
  *)
    echo "  FAIL: '-Yi1 ephem' never reached the ephemeris path, and that"
    echo "        directory has sepl_18.se1 in it. A -Yi the user typed"
    echo "        which really does hold an ephemeris must be searched."
    exit 1 ;;
esac

# And the user is told when nothing they named has one. This needs a
# directory with no ephemeris anywhere near it -- the repository root has
# sefstars.txt, and the binary's own directory is always a candidate --
# so the binary is copied somewhere bare and run from there.
tmpd=`mktemp -d`
cp "$BIN" "$tmpd/" 2>/dev/null &&
  out3=`cd "$tmpd" && $QTRUN "./\`basename $BIN\`" -Yi1 "$probe" \
        -qa 6 15 1990 12:00 0 122W19 47N36 -R1 _X <"$QTIN" 2>&1`
rm -rf "$tmpd"
case $out3 in
  *"No ephemeris files in any directory searched"*)
    echo "  ok: naming a directory with no ephemeris in it says so" ;;
  *)
    echo "  FAIL: no ephemeris anywhere and -Yi1 named by the user, and"
    echo "        nothing said so. That silence is what sends people to"
    echo "        read the path in the first place."
    exit 1 ;;
esac

# The entries are joined with ONE character. PATH_SEPARATOR is ";:" here --
# a character class Swiss hands to strchr(), its own comment reading
# "semicolon or colon may be used" -- so joining with the whole string
# works, and spends two bytes per entry rather than one while printing
# diagnostics like ".;:/usr/share/ephem".
#
# The 256-byte buffer this used to be measured against is no longer what
# makes it matter, since Astrolog now hands over only the one or two
# directories that hold an ephemeris. What is left is the diagnostic: a
# path printed with ";:" between entries reads as though something is
# wrong with it, and sends whoever is debugging one in the wrong
# direction.
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
