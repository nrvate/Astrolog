#!/bin/sh
#
# Astrolog (Version 8.00) File: tools/text-chart-capture.sh
#
# Capture the Windows build's text charts, so the Qt port can be compared
# against the real thing instead of reasoned about.
#
#   make -f Makefile.win                       # once
#   tools/text-chart-capture.sh out/win        # this script: the Windows build
#   QTTEXTDIR=out/qt ./run-qt-tests.sh         # the Qt side
#   python3 tools/text-chart-diff.py out/win out/qt
#
# The Qt side is deliberately NOT done here. "v" is a *toggle*, so driving
# it leaves each build in whatever state it started in -- which is not
# necessarily the same one, and a graphics chart then gets compared against
# a text chart and the run looks like a rendering divergence. The Qt side
# sets us.fGraphics directly instead (TextChartCaptureQt in qttest.cpp);
# this side gets the same determinism from launching with "_X".
#
# Everything runs on a private Xvfb display, so it never touches the
# desktop and "import -window root" is safe here -- see CLAUDE.md for why
# that matters on a real display.
#
# Needs: xvfb metacity xdotool imagemagick wine.

set -e

OUT=${1:-}
DISP=${DISP:-:77}

if [ -z "$OUT" ]; then
  echo "usage: $0 <output-dir>" >&2
  exit 1
fi
cd "$(dirname "$0")/.." || exit 1
[ -f ./astrolog.exe ] || { echo "build it first: make -f Makefile.win" >&2; exit 1; }
mkdir -p "$OUT"

# Chart types, as "accelerator name". Astrolog's accelerators are CASE
# SENSITIVE: "v" is the Show Graphics toggle, "V" (shift+v) is Standard
# Radix, and Alt+l and Alt+L are different commands. Sending "a" where the
# resource says "A" silently runs something else. Keep this list in step
# with rgszFile[] in TextChartCaptureQt().
CHARTS="shift+v:radix alt+shift+v:wheel shift+a:grid shift+k:calendar \
shift+j:influence shift+e:ephemeris alt+l:aspectlist alt+m:midpointlist"

# Xvfb isolates the *display*, not audio. Astrolog MessageBeeps on any
# keystroke it doesn't handle, so without this a capture run plays sound
# on the real speakers from a program nobody can see. MessageBeep is in
# user32 and reaches the audio backend even with winmm disabled, so
# disable the wine audio *drivers* -- that is what actually silences it.
WINEDLLOVERRIDES="winepulse.drv=d;winealsa.drv=d;wineoss.drv=d;\
winecoreaudio.drv=d;winmm=d;dsound=d;$WINEDLLOVERRIDES"
export WINEDLLOVERRIDES

cleanup() {
  [ -n "$APPPID" ] && kill "$APPPID" 2>/dev/null || true
  WINEDEBUG=-all wineserver -k 2>/dev/null || true   # outlives the app
  [ -n "$WMPID" ] && kill "$WMPID" 2>/dev/null || true
  [ -n "$XPID" ] && kill "$XPID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

Xvfb "$DISP" -screen 0 1200x900x24 >/dev/null 2>&1 &
XPID=$!
sleep 2
DISPLAY=$DISP xset -b 2>/dev/null || true

# Wine manages its own windows and doesn't strictly need a WM, but Qt does
# -- under a bare X server Qt's menus silently never open -- so the same
# harness serves both if anything here is ever pointed at astrolog-qt.
DISPLAY=$DISP metacity --sm-disable >/dev/null 2>&1 &
WMPID=$!
sleep 2

# "_X" clears us.fGraphics at startup: the GUI comes up in text mode and
# stays up. Deterministic, where pressing "v" is not.
DISPLAY=$DISP wine ./astrolog.exe _X >/dev/null 2>&1 &
APPPID=$!
sleep 16

WID=$(DISPLAY=$DISP xdotool search --name "Astrolog" 2>/dev/null | tail -1)
[ -n "$WID" ] || { echo "no Astrolog window appeared on $DISP" >&2; exit 1; }
echo "window $WID on $DISP"

# Drive with XTEST, never "xdotool key --window": that path uses
# XSendEvent, which Wine ignores outright. Keys look delivered, nothing
# happens, and each capture then shows the *previous* chart -- which reads
# as a redraw lag rather than as input never arriving.
send() { DISPLAY=$DISP xdotool windowactivate --sync "$WID" 2>/dev/null || true
         sleep 0.4; DISPLAY=$DISP xdotool key --clearmodifiers "$1"; sleep 1.2; }

# Wine under Xvfb does not reliably repaint between commands; a plain
# sleep is not enough. Resizing away and back forces a real expose.
repaint() {
  DISPLAY=$DISP xdotool windowsize "$WID" 890 646; sleep 0.4
  DISPLAY=$DISP xdotool windowsize "$WID" 892 648; sleep 0.6
}

for pair in $CHARTS; do
  key=${pair%%:*}; name=${pair##*:}
  send "$key"
  repaint
  sleep 0.6
  DISPLAY=$DISP import -window "$WID" "$OUT/$name.png"
  echo "  $name"
done
echo "captured to $OUT"
