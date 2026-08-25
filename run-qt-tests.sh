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
[ -x ./astrolog-qt-test ] || { echo "build it first: make -f Makefile.qt.test"; exit 1; }
exec env -u DISPLAY QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= \
  ./astrolog-qt-test "$@"
