#!/bin/sh
# Does the packaged Windows program actually START?
#
#   tools/ci-verify-windows-starts.sh out/package/astrolog-windows
#
# QT_CI_PLAN.md item 4.4 records that the Windows half of the package
# check is open and that WCLI cannot close it: astrolog.exe has no console
# entry point, so the Linux trick -- install it, run it from "/", assert
# Chiron -- does not transfer, and WCLI resolves its data through a
# different branch of SwissEnsurePath() so it does not substitute.
#
# Re-tested 2026-09-03 and that still holds. astrolog.exe blocks forever
# under "wine ... -os file", with or without a display: it is a WinMain
# program and always opens a window, so it cannot be driven to write a
# chart and exit.
#
# What CAN be asserted is weaker and still worth having, because nothing
# asserted it before: the packaged binary starts, finds everything it
# needs, and puts up its own window. That covers a statically-linked .exe
# missing a DLL, a corrupt build, and a package whose binary crashes
# immediately -- none of which a file listing or a checksum can see.
#
# Kills by the PID it started, never by name: pkill -f astrolog matches
# the maintainer's own running copy and has killed it before.
set -eu

dir=${1:?usage: ci-verify-windows-starts.sh <staged-dir>}
[ -d "$dir" ] || { echo "no such directory: $dir"; exit 2; }
[ -f "$dir/astrolog.exe" ] || { echo "$dir has no astrolog.exe"; exit 2; }
for t in wine Xvfb xdotool metacity; do
  command -v $t >/dev/null || { echo "$t not found"; exit 2; }
done

DISP=${WINSTART_DISPLAY:-:96}
Xvfb "$DISP" -screen 0 900x700x24 >/dev/null 2>&1 &
xp=$!
n=0
until DISPLAY=$DISP xdotool getdisplaygeometry >/dev/null 2>&1; do
  n=$((n+1)); [ $n -lt 100000 ] || { echo "Xvfb never came up"; kill $xp 2>/dev/null; exit 2; }
done

# A window manager, which this needed all along. Without one the window
# never gets mapped and xdotool never sees it: measured on a CI runner,
# ~100 seconds of polling and no window, while the same script found one
# in under a second locally. tools/windrive.sh -- the script that already
# drives this GUI successfully -- has started metacity from the beginning,
# and CLAUDE.md says both builds need one. This did not, and passed
# locally by luck of a different X setup.
#
# PULSE_SERVER=/nonexistent because metacity plays the X bell through the
# session's sound server, and Astrolog rings it on every keystroke it does
# not handle.
PULSE_SERVER=/nonexistent DISPLAY=$DISP metacity --sm-disable >/dev/null 2>&1 &
wm=$!

# Keep the program's own output. The first version sent it to /dev/null
# and then, on failure, printed four lines of speculation about what might
# have gone wrong -- while Wine's account of it had already been thrown
# away. That is the same mistake tools/package-macos.sh documents making
# and fixing: a check that discards the evidence it exists to collect.
log=$(mktemp)
trap 'rm -f "$log"' EXIT
( cd "$dir" && DISPLAY=$DISP WINEDEBUG=-all wine ./astrolog.exe >"$log" 2>&1 ) &
ap=$!
# Search for ASTROLOG'S window, not for any window. With a window manager
# running there is always at least one -- metacity has its own -- and the
# first version took the last id xdotool returned, which was metacity's.
#
# A DEADLINE, not an iteration count. The first version polled 4000 times
# and that was a proxy for time that varies by machine: locally each pass
# costs enough that 4000 is minutes, on a CI runner two xdotool calls take
# about 4 ms and the whole loop expired in EIGHTEEN SECONDS -- long before
# Wine had finished starting a GUI for the first time in a fresh prefix.
# It reported the program as never opening a window when it had not been
# given a chance to.
deadline=$(( $(date +%s) + ${WINSTART_TIMEOUT:-180} ))
wid=""; died=""; status=""
while [ -z "$wid" ] && [ "$(date +%s)" -lt "$deadline" ]; do
  wid=$(DISPLAY=$DISP xdotool search --onlyvisible --name '^Astrolog' 2>/dev/null | tail -1)
  [ -n "$wid" ] && break
  # A program that has exited is not going to open a window later, and
  # waiting the remaining deadline out teaches nothing. But "wine has
  # returned" is not quite "the app is gone" -- the launcher can hand off
  # -- so this shortens the wait to a grace period rather than failing on
  # the spot, and reports the exit status either way.
  if [ -z "$died" ] && ! kill -0 $ap 2>/dev/null; then
    died=yes; status=0
    wait $ap 2>/dev/null || status=$?
    grace=$(( $(date +%s) + 10 ))
    [ "$grace" -lt "$deadline" ] && deadline=$grace
  fi
  sleep 0.25
done
title=""
[ -n "$wid" ] && title=$(DISPLAY=$DISP xdotool getwindowname "$wid" 2>/dev/null || true)

# Collect what is on the display BEFORE tearing it down. The old version
# recorded only whether some other window existed, which answered "is the
# display working" and nothing else; the names say whether metacity is
# alone up there or Wine put up a crash dialog nobody could see.
wins=""
[ -n "$wid" ] || wins=$(DISPLAY=$DISP xdotool search --onlyvisible --name . 2>/dev/null \
  | while read -r w; do
      printf '     %-10s %s\n' "$w" \
        "$(DISPLAY=$DISP xdotool getwindowname "$w" 2>/dev/null || echo '(unnamed)')"
    done)

kill $ap 2>/dev/null || true
DISPLAY=$DISP WINEDEBUG=-all wineserver -k 2>/dev/null || true
kill $wm 2>/dev/null || true
kill $xp 2>/dev/null || true
wait 2>/dev/null || true

[ -n "$wid" ] || {
  echo "THE PACKAGED PROGRAM NEVER OPENED A WINDOW."
  if [ -n "$died" ]; then
    echo "== The process EXITED, status $status, without mapping a window."
    echo "== It is statically linked and needs no runtime, so this is a"
    echo "== crash on startup or a corrupt binary, not a missing DLL."
  else
    echo "== The process was still running when the ${WINSTART_TIMEOUT:-180}s"
    echo "== deadline expired -- it started and hung rather than crashing."
    echo "== Raise WINSTART_TIMEOUT if a slow runner is the real answer."
  fi
  if [ -n "$wins" ]; then
    echo "== Windows that WERE mapped (so the display and WM work):"
    printf '%s\n' "$wins"
  else
    echo "== Nothing mapped a window at all, not even metacity, so"
    echo "== suspect the display rather than the program."
  fi
  echo "== What the program said:"
  if [ -s "$log" ]; then
    sed 's/^/     /' "$log" | head -40
  else
    echo "     (nothing on stdout or stderr)"
  fi
  exit 1; }
echo "windows package starts: window titled '$title'"
