#!/bin/sh
# Drive the real Windows build under Wine, on a private display.
#
#   tools/windrive.sh run tools/scenarios/win-objectsel.txt
#   tools/windrive.sh run SCRIPT --args "-i nrvate.as"
#   tools/windrive.sh shell            # leave it running, print the display
#
# Wine has no AT-SPI, so unlike tools/qtdrive.sh this cannot address a
# widget by name -- there is no way around keys and coordinates over there.
# What it does remove is every part of the setup that went wrong by hand:
#
#   -Wt              PrintWarning() is a modal MessageBox on Windows and
#                    will sit there forever in an unattended run, with the
#                    menu bar painted and the client area blank. This is
#                    the single most expensive failure mode in this repo.
#   WINEDLLOVERRIDES disables the wine audio drivers. MessageBeep() lives
#                    in user32 and reaches the audio backend even with winmm
#                    off, so without this a headless run makes noise on the
#                    real speakers from a window nobody can see.
#   no window manager  Wine manages its own windows and does not need one.
#                    Running metacity here would play the X bell through the
#                    user's PulseAudio on every keystroke Astrolog ignores.
#   wineserver -k    it outlives the app, and a leftover one makes the next
#                    run behave strangely.
#
# Scenario commands, one per line, "#" comments ignored:
#
#   menu f g                 alt+f, then the entry's mnemonic (g)
#   focus Object Selections  give a window X input focus, by title
#   key Return               xdotool key, sent via XTEST
#   type out.as              xdotool type
#   expect-window Astrolog   fail unless a window with that title exists
#   expect-no-window X       fail unless none does
#   shot out/win/x.png       screenshot the whole private display
#   windows                  list what is mapped, for working out a scenario
#   sleep 2
#
# Exit status is 0 only if every expect-* passed. Note the checks are the
# point: a keystroke that goes nowhere is silent, so a scenario without
# them proves nothing.
set -e
cd "$(dirname "$0")/.." || exit 1
[ -f ./astrolog.exe ] || { echo "build it first: make -f Makefile.win"; exit 1; }

DISP=${WINDRIVE_DISPLAY:-:91}
MODE=${1:-run}
SCRIPT=$2
ARGS=""
[ "$3" = "--args" ] && ARGS=$4

WINEDLLOVERRIDES="winepulse.drv=d;winealsa.drv=d;wineoss.drv=d;\
winecoreaudio.drv=d;winmm=d;dsound=d"
export WINEDLLOVERRIDES WINEDEBUG=-all

cleanup() {
  WINEDEBUG=-all wineserver -k 2>/dev/null
  [ -n "$WMPID" ] && kill "$WMPID" 2>/dev/null
  [ -n "$XPID" ] && kill "$XPID" 2>/dev/null
  wait 2>/dev/null
  return 0
}
trap cleanup EXIT INT TERM

# Bigger than the real config asks for. nrvate.as sets ":Xw 1260 1260"
# and the sidebar adds 240, so the window is 1500x1260; on a smaller
# screen it overflows and the menu bar is off the visible area, which
# looks exactly like the app failing to draw.
Xvfb "$DISP" -screen 0 1920x1200x24 >/dev/null 2>&1 &
XPID=$!
sleep 2
# Wine renders fine with no window manager, which is why the older capture
# script runs without one. Input focus is a different matter: a dialog Wine
# creates does not become the X focus window on its own, so keys keep going
# to the main window and the dialog looks like it ignores everything.
# metacity is started with no route to the sound server, because it plays
# the X bell and Astrolog rings it on every keystroke it does not handle.
PULSE_SERVER=/nonexistent DISPLAY=$DISP metacity --sm-disable >/dev/null 2>&1 &
WMPID=$!
sleep 2

DISPLAY=$DISP wine ./astrolog.exe -Wt $ARGS >/dev/null 2>&1 &
# Wine is slow to first paint and a scenario that starts too early just
# sends keys into nothing. Wait for the window rather than guessing.
i=0
while [ $i -lt 40 ]; do
  DISPLAY=$DISP xdotool search --onlyvisible --name "Astrolog" >/dev/null 2>&1 && break
  i=$((i+1)); sleep 1
done
[ $i -lt 40 ] || { echo "FAIL: the app never mapped a window"; exit 1; }
W=$(DISPLAY=$DISP xdotool search --onlyvisible --name "Astrolog" | head -1)
DISPLAY=$DISP xdotool windowactivate "$W" 2>/dev/null || true
sleep 1
# Wine under Xvfb does not reliably repaint on its own: the window comes up
# with a half drawn menu bar and a black client area, which reads exactly
# like the app failing to start. Sleeping does not help. Resizing away and
# back forces a real expose.
eval "$(DISPLAY=$DISP xdotool getwindowgeometry --shell "$W")"
DISPLAY=$DISP xdotool windowsize "$W" $((WIDTH-40)) $((HEIGHT-40))
sleep 1
DISPLAY=$DISP xdotool windowsize "$W" "$WIDTH" "$HEIGHT"
sleep 2
# Resizing loses the focus set above, so take it back before any keys go out.
DISPLAY=$DISP xdotool windowactivate "$W" 2>/dev/null || true
sleep 1

if [ "$MODE" = "shell" ]; then
  echo "running on $DISP, window $W -- press Enter to stop"
  read _
  exit 0
fi

[ -n "$SCRIPT" ] || { echo "usage: $0 run SCRIPT [--args \"...\"]"; exit 2; }

# How many windows are mapped with no menu open, so an opened menu can be
# told apart from one that never appeared.
nWinBase=$(DISPLAY=$DISP xdotool search --onlyvisible --name . 2>/dev/null | wc -l)

fails=0
while IFS= read -r line; do
  case "$line" in ''|'#'*) continue;; esac
  cmd=${line%% *}; rest=${line#* }
  [ "$rest" = "$line" ] && rest=""
  status=""
  case "$cmd" in
    menu)
      # Sending the item mnemonic into a menu that never opened is
      # completely silent -- it just does nothing, and the scenario reads
      # as if the menu entry is broken. An open menu is one more mapped
      # window than the baseline, so check that before the second key and
      # retry rather than reporting a failure the app did not cause.
      top=${rest%% *}; item=${rest#* }
      n=0
      while [ $n -lt 4 ]; do
        DISPLAY=$DISP xdotool key --clearmodifiers "alt+$top"; sleep 1
        cnt=$(DISPLAY=$DISP xdotool search --onlyvisible --name . 2>/dev/null | wc -l)
        [ "$cnt" -gt "$nWinBase" ] && break
        n=$((n+1))
      done
      DISPLAY=$DISP xdotool key --clearmodifiers "$item"; sleep 2 ;;
    focus)
      # With no window manager a dialog Wine has just created does not take
      # X input focus, so XTEST keys keep going to the main window and the
      # dialog appears to ignore everything. Point focus at it by title.
      fw=$(DISPLAY=$DISP xdotool search --onlyvisible --name "$rest" | tail -1)
      if [ -n "$fw" ]; then
        DISPLAY=$DISP xdotool windowactivate "$fw" 2>/dev/null || \
          DISPLAY=$DISP xdotool windowfocus "$fw" 2>/dev/null || true
        sleep 1
      else status="FAIL"; fails=$((fails+1)); fi ;;
    key)   DISPLAY=$DISP xdotool key --clearmodifiers $rest; sleep 1 ;;
    type)  DISPLAY=$DISP xdotool type --delay 40 -- "$rest"; sleep 1 ;;
    sleep) sleep "$rest" ;;
    windows)
      DISPLAY=$DISP xdotool search --onlyvisible --name . 2>/dev/null | \
        while read -r w; do
          echo "    $w '$(DISPLAY=$DISP xdotool getwindowname "$w" 2>/dev/null)'"
        done ;;
    shot)
      mkdir -p "$(dirname "$rest")"
      DISPLAY=$DISP import -window root "$rest" ;;
    expect-window)
      DISPLAY=$DISP xdotool search --onlyvisible --name "$rest" >/dev/null 2>&1 \
        || { status="FAIL"; fails=$((fails+1)); } ;;
    expect-no-window)
      if DISPLAY=$DISP xdotool search --onlyvisible --name "$rest" >/dev/null 2>&1
      then status="FAIL"; fails=$((fails+1)); fi ;;
    *) status="FAIL (unknown)"; fails=$((fails+1)) ;;
  esac
  printf '  %-56s %s\n' "$line" "$status"
done < "$SCRIPT"

if [ $fails -eq 0 ]; then echo "PASS: 0 command(s) failed"; else
  echo "FAIL: $fails command(s) failed"; fi
exit $([ $fails -eq 0 ] && echo 0 || echo 1)
