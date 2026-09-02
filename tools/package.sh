#!/bin/sh
# Assemble a runnable package for one platform.
#
#   tools/package.sh windows [outdir]
#
# QT_CI_PLAN.md item 4.3. The Windows one is the easy one: Makefile.win
# links -static -static-libgcc -static-libstdc++, so the package is one
# .exe plus the data files, with no DLLs to chase.
#
# The Linux package is deliberately absent. Q1 is still open -- AppImage
# (~70 MB of Qt closure), a tarball with a documented apt prerequisite, or
# a .deb (~8 MB, apt resolves Qt5) -- and that is a judgement about
# audience, not something to settle by writing whichever is easiest.
#
# WHAT SHIPS is not obvious and each entry was checked rather than
# guessed:
#   ephem/          the bundled ephemeris. Without it Chiron, Ceres and
#                   Eris read 0Ari00 -- and the Sun does NOT move, because
#                   Astrolog falls back to Moshier in silence.
#   font/           the chart fonts.
#   *.as            settings, atlas, timezone changes. NOT nrvate.as.
#   sefstars.txt    fixed stars, which need no .se1 at all.
#   seorbel.txt     orbital elements.
#   astexo.csv      1.1 MB, read at charts3.cpp:1792 under gs.fAllExo --
#                   the -XUx switch and the "Show E&xoplanets" menu item.
#                   Omitting it gives a menu item that silently draws
#                   nothing.
#   earth.bmp       the world map bitmap.
#   *.htm           the documentation the program's own help refers to.
#
# WHAT MUST NOT SHIP: nrvate.as, which is the maintainer's personal
# settings file and points -Yi1 at a NAS mount that exists on one machine
# in the world; and any source.
set -eu

plat=${1:?usage: package.sh windows [outdir]}
out=${2:-out/package}
cd "$(dirname "$0")/.."

case $plat in
  windows) bin=astrolog.exe ;;
  *) echo "unknown platform '$plat' -- only 'windows' is implemented; see Q1."; exit 2 ;;
esac

[ -f "$bin" ] || { echo "build it first: make -f Makefile.win"; exit 1; }

dir="$out/astrolog-$plat"
rm -rf "$dir"
mkdir -p "$dir"

cp "$bin" "$dir/"
cp -r ephem font "$dir/"
cp astrolog.as atlas.as timezone.as "$dir/"
cp sefstars.txt seorbel.txt astexo.csv earth.bmp "$dir/"
cp astrolog.htm changes.htm license.htm "$dir/"

# actions/upload-artifact does not preserve unix modes, so a binary can
# arrive 0644 and refuse to execute. swisseph's first tagged release
# shipped a swetest that answered "Permission denied". Set it here and
# read it back out of the archive, never off the disk (item 4.6).
chmod 0755 "$dir/$bin"

# The manifest is written here and verified by tools/ci-verify-package.sh,
# which never writes one. Two scripts rather than one because a check that
# generates what it verifies cannot fail -- see that script's step 5.
# LF, no BOM, trailing newline: a SHA256SUMS written by PowerShell's
# Set-Content gets CRLF, and sha256sum -c then fails on every line on
# every platform.
( cd "$dir" && find . -type f ! -name SHA256SUMS -print0 \
    | sort -z | xargs -0 sha256sum > SHA256SUMS )

echo "== packaged $plat: $(find "$dir" -type f | wc -l) files, $(du -sh "$dir" | cut -f1)"
echo "$dir"
