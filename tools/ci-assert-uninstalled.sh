#!/bin/sh
# Assert "make uninstall" removed everything "make install" wrote.
#
#   tools/ci-assert-uninstalled.sh "$HOME/.local"
#
# QT_CI_PLAN.md item 4.8. "make uninstall" is documented in README.md and
# CLAUDE.md and had never been run by anything: a target nobody executes
# is in the same category as a configuration nobody compiles, and this
# repository has now found three of those broken.
#
# It checks the removal is COMPLETE and no wider than it should be. An
# uninstall that leaves a dangling wrapper on PATH is a program that
# reports "No such file or directory" months later; one that removes the
# whole icon directory takes other applications' icons with it.
set -eu

prefix=${1:?usage: ci-assert-uninstalled.sh <prefix>}
bad=0

for f in "$prefix/bin/astrolog" "$prefix/bin/astrolog-qt" \
         "$prefix/share/applications/astrolog.desktop"; do
  if [ -e "$f" ]; then echo "STILL PRESENT: $f"; bad=1; fi
done
for s in 16 32 48; do
  f="$prefix/share/icons/hicolor/${s}x${s}/apps/astrolog.png"
  if [ -e "$f" ]; then echo "STILL PRESENT: $f"; bad=1; fi
done

# The directories themselves must survive: they are shared with every
# other application on the system, and an uninstall that removes
# ~/.local/share/applications is a bug that only shows up as somebody
# else's missing menu entry.
for d in "$prefix/bin" "$prefix/share/applications" \
         "$prefix/share/icons/hicolor/48x48/apps"; do
  if [ ! -d "$d" ]; then
    echo "REMOVED A SHARED DIRECTORY: $d"
    echo "== uninstall must remove its own files, not the directories it"
    echo "== put them in -- those belong to every application."
    bad=1
  fi
done

[ "$bad" -eq 0 ] || exit 1
echo "uninstalled cleanly: nothing of ours left, shared directories intact"
