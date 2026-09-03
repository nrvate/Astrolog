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
for t in wine Xvfb xdotool; do
  command -v $t >/dev/null || { echo "$t not found"; exit 2; }
done

DISP=${WINSTART_DISPLAY:-:96}
Xvfb "$DISP" -screen 0 900x700x24 >/dev/null 2>&1 &
xp=$!
n=0
until DISPLAY=$DISP xdotool getdisplaygeometry >/dev/null 2>&1; do
  n=$((n+1)); [ $n -lt 100000 ] || { echo "Xvfb never came up"; kill $xp 2>/dev/null; exit 2; }
done

( cd "$dir" && DISPLAY=$DISP WINEDEBUG=-all wine ./astrolog.exe >/dev/null 2>&1 ) &
ap=$!
wid=""; n=0
while [ $n -lt 4000 ] && [ -z "$wid" ]; do
  wid=$(DISPLAY=$DISP xdotool search --onlyvisible --name . 2>/dev/null | tail -1)
  n=$((n+1))
done
title=""
[ -n "$wid" ] && title=$(DISPLAY=$DISP xdotool getwindowname "$wid" 2>/dev/null || true)

kill $ap 2>/dev/null || true
DISPLAY=$DISP WINEDEBUG=-all wineserver -k 2>/dev/null || true
kill $xp 2>/dev/null || true
wait 2>/dev/null || true

[ -n "$wid" ] || {
  echo "THE PACKAGED PROGRAM NEVER OPENED A WINDOW."
  echo "== It is statically linked and needs no runtime, so this is a"
  echo "== crash on startup or a corrupt binary, not a missing DLL."
  exit 1; }
case $title in
  Astrolog*) ;;
  *) echo "A WINDOW APPEARED BUT IS NOT ASTROLOG'S: '$title'"; exit 1 ;;
esac
echo "windows package starts: window titled '$title'"
