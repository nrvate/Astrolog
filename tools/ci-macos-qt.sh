#!/bin/sh
# A PINNED Qt for the macOS job, made visible to Makefile.qt.
#
#   tools/ci-macos-qt.sh <version> <dir>      # e.g. 6.8.3 "$HOME/Qt"
#
# Prints the environment the later steps need, in the form a workflow
# appends to $GITHUB_ENV: PKG_CONFIG_PATH, PATH and MACDEPLOYQT.
#
# Why. The job ran "brew install qt", so the .dmg a release shipped was
# built against whatever Qt Homebrew carried that day, on whatever macOS
# "macos-latest" meant -- on the one platform nobody working on this can
# open. Windows pins Qt 6.8.3 through aqtinstall and caches it; this does
# the same for macOS, with the same version, so a release cut from an
# unchanged commit is built the same way twice.
#
# Two facts about the Qt installer's macOS package, measured 2026-09-05
# by downloading it onto a Linux machine with the same aqt command:
#
# - It has NO pkg-config files ("no-pkg-config" is in its qconfig.pri)
#   and no include/QtCore symlinks: the headers live inside the
#   frameworks, lib/QtCore.framework/Headers. Makefile.qt finds Qt
#   through pkg-config and nothing else, so this writes the five .pc
#   files Makefile.qt asks for, framework-style, and checks that
#   pkg-config resolves them before printing anything.
#
# - The qtbase+qttools archives are 1.8 GB, of which the debug-symbol
#   .dSYM bundles are most; they are removed, and what is left is what
#   the cache keeps. macdeployqt is in qttools.
set -eu
ver=${1:?usage: ci-macos-qt.sh <version> <dir>}
dir=${2:?usage: ci-macos-qt.sh <version> <dir>}
q=$dir/$ver/macos

if [ ! -x "$q/bin/macdeployqt" ]; then
  echo "== installing Qt $ver into $dir (aqtinstall 3.3.0, qtbase + qttools)" >&2
  python3 -m pip install -q "aqtinstall==3.3.0" >&2
  python3 -m aqt install-qt mac desktop "$ver" clang_64 --archives qtbase qttools -O "$dir" >&2
  find "$q" -name '*.dSYM' -type d -prune -exec rm -rf {} + 2>/dev/null || true
else
  echo "== Qt $ver already in $dir" >&2
fi
[ -x "$q/bin/macdeployqt" ] || { echo "no macdeployqt under $q -- the install did not produce a Qt" >&2; exit 1; }
[ -d "$q/lib/QtWidgets.framework" ] || { echo "no QtWidgets.framework under $q/lib" >&2; exit 1; }

mkdir -p "$q/lib/pkgconfig"
for m in Core Gui Widgets PrintSupport Network; do
  case $m in
    Core) req="" ;;
    Gui)  req="Qt6Core" ;;
    *)    req="Qt6Core Qt6Gui" ;;
  esac
  up=$(printf '%s' "$m" | tr '[:lower:]' '[:upper:]')
  {
    echo "prefix=$q"
    echo 'libdir=${prefix}/lib'
    echo "Name: Qt6 $m"
    echo "Description: Qt $m module, from the Qt installer; this file was written by tools/ci-macos-qt.sh"
    echo "Version: $ver"
    echo "Libs: -F\${libdir} -framework Qt$m"
    echo "Cflags: -F\${libdir} -I\${libdir}/Qt$m.framework/Headers -DQT_${up}_LIB"
    [ -z "$req" ] || echo "Requires: $req"
  } > "$q/lib/pkgconfig/Qt6$m.pc"
done

PKG_CONFIG_PATH=$q/lib/pkgconfig pkg-config --exists Qt6Widgets Qt6Gui Qt6Core Qt6PrintSupport Qt6Network || {
  echo "pkg-config cannot resolve the .pc files just written under $q/lib/pkgconfig" >&2; exit 1; }
echo "== pkg-config resolves Qt $(PKG_CONFIG_PATH=$q/lib/pkgconfig pkg-config --modversion Qt6Widgets)" >&2

echo "PKG_CONFIG_PATH=$q/lib/pkgconfig"
echo "PATH=$q/bin:$PATH"
echo "MACDEPLOYQT=$q/bin/macdeployqt"
