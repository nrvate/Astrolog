#!/bin/sh
# Assert the installed desktop entry is valid and its icon exists.
#
#   tools/ci-assert-desktop.sh "$HOME/.local"
#
# QT_CI_PLAN.md item 4.8. A malformed .desktop file does not error --
# it simply does not appear in the menu, which is invisible until a user
# says "I installed it and there's nothing in my applications list". The
# same is true of an Icon= naming something that was never installed:
# the entry appears with a blank square.
#
# Both are generated in two places -- the Makefile's install target and
# tools/package-stage.sh -- so they can drift from each other as well as
# from the spec.
set -eu

prefix=${1:?usage: ci-assert-desktop.sh <prefix>}
entry="$prefix/share/applications/astrolog.desktop"
bad=0

[ -f "$entry" ] || { echo "NO DESKTOP ENTRY: $entry"; exit 1; }

if command -v desktop-file-validate >/dev/null; then
  desktop-file-validate "$entry" || { echo "== the entry above is invalid"; bad=1; }
else
  echo "desktop-file-validate absent -- install desktop-file-utils; checking"
  echo "only the parts this script can check itself."
fi

# Exec= must point at something that exists and runs.
exec_path=$(sed -n 's/^Exec=//p' "$entry" | head -1 | awk '{print $1}')
[ -x "$exec_path" ] || { echo "Exec= points at $exec_path, which is not executable"; bad=1; }

# Icon= is a NAME, not a path: the lookup is
# <prefix>/share/icons/hicolor/<size>/apps/<name>.png. An entry naming an
# icon nobody installed shows a blank square rather than an error.
icon=$(sed -n 's/^Icon=//p' "$entry" | head -1)
[ -n "$icon" ] || { echo "no Icon= in the entry"; bad=1; }
found=$(find "$prefix/share/icons/hicolor" -name "$icon.png" 2>/dev/null | wc -l)
if [ "$found" -eq 0 ]; then
  echo "Icon=$icon names no installed icon under $prefix/share/icons/hicolor"
  bad=1
fi

[ "$bad" -eq 0 ] || exit 1
echo "desktop entry ok: valid, Exec runs, Icon=$icon resolves at $found sizes"
