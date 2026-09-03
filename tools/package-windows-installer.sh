#!/bin/sh
# Build a Windows installer (.exe) from a staged package directory.
#
#   tools/package-windows-installer.sh <staged-dir> <outfile>
#
# NSIS, and makensis runs natively on Linux -- which is the whole reason
# this can live in the same ubuntu-22.04 job that cross-compiles
# astrolog.exe with mingw. Inno Setup would need Wine, WiX needs Windows
# and .NET, and a 7-Zip SFX is an extractor rather than an installer: no
# Start Menu entry, no uninstaller, no Add/Remove Programs row.
#
# The .zip stays. An installer is what most people want and a zip is what
# the rest want -- someone putting Astrolog on a machine they do not
# administer, or keeping several versions side by side, cannot use an
# installer that writes to Program Files and HKLM.
set -e

src=${1:?usage: package-windows-installer.sh <staged-dir> <outfile>}
out=${2:?usage: package-windows-installer.sh <staged-dir> <outfile>}

command -v makensis >/dev/null || {
  echo "makensis not found: apt-get install nsis"; exit 2; }
[ -d "$src" ] || { echo "no such directory: $src"; exit 2; }
[ -f "$src/astrolog.exe" ] || {
  echo "$src has no astrolog.exe -- stage the package first"; exit 2; }

ver=$(tools/ci-assert-version.sh)
srcabs=$(cd "$src" && pwd)
outabs=$(mkdir -p "$(dirname "$out")" && cd "$(dirname "$out")" && pwd)/$(basename "$out")

# NSIS wants Windows-style separators in paths it embeds.
makensis -V2 \
  "-DVERSION=$ver" \
  "-DSRCDIR=$srcabs" \
  "-DOUTFILE=$outabs" \
  tools/astrolog.nsi

[ -f "$outabs" ] || { echo "makensis produced no $outabs"; exit 1; }
ls -lh "$outabs" | awk '{print "   "$5"  "$9}'

# It must be a PE executable, not whatever else a failed run leaves.
case $(file -b "$outabs") in
  *PE32*|*MS\ Windows*) echo "   is a Windows executable" ;;
  *) echo "   NOT a Windows executable: $(file -b "$outabs")"; exit 1 ;;
esac
