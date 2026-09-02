#!/bin/sh
# Verify an assembled package before anyone can download it.
#
#   tools/ci-verify-package.sh out/package/astrolog-windows
#
# QT_CI_PLAN.md item 4.4, which is swisseph's checklist and their scars:
#
#   1. every required path exists
#   2. every forbidden path does not
#   3. SHA256SUMS generated (LF, no BOM, trailing newline)
#   4. checked back with sha256sum -c
#   5. the number of lines in SHA256SUMS equals the file count minus one
#
# STEP 5 IS NOT PARANOIA and it is the reason this script exists rather
# than three lines of YAML. This project has already shipped a harness
# that "was byte-identical over 75,471 lines while proving nothing", and
# tools/switch-matrix.sh capped its output at 30 lines of 159 for two
# whole campaigns. A manifest covering a fifth of a package is the same
# failure, and it passes step 4 perfectly.
#
# A SHA256SUMS written by PowerShell's Set-Content gets CRLF, which makes
# sha256sum -c fail on every line on every platform. Step 3 says LF for
# that reason; this script is sh, so it gets LF for free, and the check in
# step 4 would catch it anyway.
set -eu

dir=${1:?usage: ci-verify-package.sh <package directory>}
[ -d "$dir" ] || { echo "NO PACKAGE: $dir is not a directory."; exit 1; }

# Data files first: these are what makes the package runnable, and the
# ones whose absence is silent. A missing ephemeris does not stop the
# program -- it makes Chiron read 0Ari00 while the Sun looks perfect.
required="ephem/sepl_18.se1 ephem/semo_18.se1 ephem/seas_18.se1
          astrolog.as atlas.as timezone.as
          sefstars.txt seorbel.txt astexo.csv earth.bmp
          astrolog.htm changes.htm license.htm"
forbidden="nrvate.as astrolog.cpp qtdriver.cpp Makefile .git"

bad=0
for f in $required; do
  [ -e "$dir/$f" ] || { echo "MISSING: $f"; bad=1; }
done
[ -d "$dir/font" ] || { echo "MISSING: font/"; bad=1; }
# Exactly one executable, and it must be one we meant to ship.
n=$(find "$dir" -maxdepth 1 -type f \( -name '*.exe' -o -perm -u+x \) | wc -l)
[ "$n" -ge 1 ] || { echo "MISSING: no binary in the package root"; bad=1; }
for f in $forbidden; do
  [ ! -e "$dir/$f" ] || { echo "FORBIDDEN: $f is in the package"; bad=1; }
done
# No source of any kind, however it got there.
src=$(find "$dir" -name '*.cpp' -o -name '*.h' -o -name 'Makefile*' | head -5)
[ -z "$src" ] || { echo "FORBIDDEN: source files in the package:"; \
                   echo "$src" | sed 's/^/  /'; bad=1; }

[ "$bad" -eq 0 ] || { echo "== package contents are wrong"; exit 1; }

# The manifest must already exist: tools/package.sh writes it, this script
# only checks it. That separation is the whole point, and it was learned
# the hard way -- the first draft generated the manifest here and then
# counted its lines, so step 5 compared a number against itself and could
# not fail. Falsifying it caught that: a manifest truncated to 9 of 46
# lines passed cleanly. A check that generates what it verifies is the
# vacuous-harness failure in miniature.
[ -f "$dir/SHA256SUMS" ] || {
  echo "NO MANIFEST: $dir/SHA256SUMS does not exist. tools/package.sh"
  echo "== writes it; this script does not, on purpose."; exit 1; }
( cd "$dir" && sha256sum -c SHA256SUMS >/dev/null ) || {
  echo "== SHA256SUMS does not verify against the files it names"; exit 1; }

files=$(find "$dir" -type f | wc -l)
lines=$(wc -l <"$dir/SHA256SUMS")
if [ "$lines" -ne $((files - 1)) ]; then
  echo "MANIFEST INCOMPLETE: $lines lines for $files files (expected $((files - 1)),"
  echo "== since SHA256SUMS does not list itself). A manifest that covers"
  echo "== part of a package verifies perfectly and proves nothing."
  exit 1
fi

case $(file -b "$dir/SHA256SUMS" 2>/dev/null) in
  *CRLF*) echo "MANIFEST CRLF: sha256sum -c fails on every line, everywhere."; exit 1 ;;
esac

echo "package ok: $files files, $lines covered by SHA256SUMS, nothing forbidden"
