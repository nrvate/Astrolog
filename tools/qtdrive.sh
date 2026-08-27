#!/bin/sh
# Wrapper for tools/qtdrive.py: brings up a private display, window manager
# and D-Bus session, then runs the driver inside them.
#
#   tools/qtdrive.sh tree
#   tools/qtdrive.sh tree --args "-i nrvate.as"
#   tools/qtdrive.sh run tools/scenarios/objectsel.txt
#
# Everything is private to this run:
#   - Xvfb, so it never draws on the user's screen and "import -window root"
#     is safe here (see CLAUDE.md, where it is forbidden on a real display);
#   - a D-Bus session of its own, so turning on ScreenReaderEnabled -- which
#     is what makes Qt publish its widget tree -- does not touch the user's
#     desktop settings;
#   - metacity, because Qt menus silently never open without a window
#     manager, started with no route to the sound server because it plays
#     the X bell through the real speakers and Astrolog rings it on every
#     keystroke it doesn't handle.
set -e
cd "$(dirname "$0")/.." || exit 1
[ -x ./astrolog-qt ] || { echo "build it first: make -f Makefile.qt"; exit 1; }

DISP=${QTDRIVE_DISPLAY:-:90}
export QTDRIVE_DISPLAY=$DISP

cleanup() {
  [ -n "$WMPID" ] && kill "$WMPID" 2>/dev/null
  [ -n "$XPID" ] && kill "$XPID" 2>/dev/null
  pkill -x astrolog-qt 2>/dev/null
  return 0
}
trap cleanup EXIT INT TERM

Xvfb "$DISP" -screen 0 1280x1024x24 >/dev/null 2>&1 &
XPID=$!
sleep 2
PULSE_SERVER=/nonexistent DISPLAY=$DISP metacity --sm-disable >/dev/null 2>&1 &
WMPID=$!
sleep 1

# The driver needs the a11y bus of whatever session it is in, so the whole
# thing runs under one private bus.
exec dbus-run-session -- /bin/sh -c '
  DISPLAY='"$DISP"' /usr/libexec/at-spi-bus-launcher --launch-immediately &
  sleep 2
  gdbus call --session --dest org.a11y.Bus --object-path /org/a11y/bus \
    --method org.freedesktop.DBus.Properties.Set \
    org.a11y.Status ScreenReaderEnabled "<true>" >/dev/null 2>&1
  DISPLAY='"$DISP"' exec python3 tools/qtdrive.py "$@"
' _ "$@"
