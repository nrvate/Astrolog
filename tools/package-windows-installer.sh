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

# VIProductVersion takes four dot-separated NUMBERS and nothing else --
# makensis rejects "8.00-qt.3" outright. Derive 8.0.0.3 from it: the two
# components of the upstream version, a zero, and the fork number that
# szVersionFork owns. Anything that does not parse is a bug in the version
# string, not something to paper over with a default.
quad=$(echo "$ver" | sed -n \
  's/^\([0-9][0-9]*\)\.\([0-9][0-9]*\)-qt\.\([0-9][0-9]*\)$/\1 \2 \3/p')
[ -n "$quad" ] || {
  echo "cannot derive a numeric version from '$ver' (want N.N-qt.N)"; exit 1; }
# Strip leading zeros -- "8.00-qt.3" gives a middle field of "00", and a
# version resource of "8.00.0.3" is not what Explorer should show.
set -- $quad
quad="$(echo "$1" | sed 's/^0*\([0-9]\)/\1/').$(echo "$2" | sed 's/^0*\([0-9]\)/\1/').0.$(echo "$3" | sed 's/^0*\([0-9]\)/\1/')"
srcabs=$(cd "$src" && pwd)
outabs=$(mkdir -p "$(dirname "$out")" && cd "$(dirname "$out")" && pwd)/$(basename "$out")

# NSIS wants Windows-style separators in paths it embeds.
makensis -V2 \
  "-DVERSION=$ver" \
  "-DVERSIONQUAD=$quad" \
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
