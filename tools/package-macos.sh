#!/bin/sh
# Build Astrolog.app and a .dmg from it. macOS only; run on a runner or a
# Mac with Homebrew Qt6 installed.
#
#   tools/package-macos.sh [outdir]        # default: out/macos
#
# What this ships, and what it deliberately does not:
#
#   - an ad-hoc signature (codesign -s -). Free, no Apple account, and on
#     Apple Silicon it is not optional: arm64 refuses to execute an
#     unsigned binary at all.
#   - NOT notarization. That needs a Developer ID (~$99/yr) and a secret
#     in the repository, which QT_CI_PLAN.md ground rule 5 forbids. The
#     consequence is concrete and belongs in the release notes rather
#     than in a surprise: anyone who downloads this through a browser
#     gets a quarantine attribute and Gatekeeper refuses to open it until
#     they either right-click -> Open, or run
#         xattr -dr com.apple.quarantine /Applications/Astrolog.app
#     A download fetched with curl gets no quarantine attribute and just
#     runs.
#
# The data files live in Contents/MacOS beside the binary rather than in
# Contents/Resources, which is unconventional and deliberate. Astrolog
# resolves ephem/, font/, the .as files and the icons relative to its own
# executable, in five separate places across io.cpp, calc.cpp,
# qtdriver.cpp and qtdialog.cpp. Teaching all five about ../Resources is
# the tidier bundle and a change to shared core for one platform's
# benefit; putting the data where the program already looks costs
# nothing and cannot regress Linux or Windows. QT_CI_PLAN.md 9.4 has the
# longer argument.
set -e

# ABSOLUTE, and this is not tidiness. Astrolog resolves a -Yi path that
# starts with a letter RELATIVE TO ITS OWN EXECUTABLE, not to the working
# directory -- SwissEnsurePath() says so: "If dir is relative path, then
# prepend the path to executable". Inside a bundle the executable lives in
# Contents/MacOS, so a cwd-relative "out/macos/Astrolog.app/Contents/
# MacOS/ephem" became
#
#   .../Contents/MacOS/out/macos/Astrolog.app/Contents/MacOS/ephem
#
# and every body read 0Ari00. That failed the v8.00-qt.3 release, and it
# reproduces on Linux in three commands with the binary copied into a
# fake Contents/MacOS -- which is where it was diagnosed, rather than by
# another round trip through a macOS runner.
out=$(mkdir -p "${1:-out/macos}" && cd "${1:-out/macos}" && pwd)
[ "$(uname)" = Darwin ] || { echo "macOS only (uname says $(uname))"; exit 2; }
[ -x ./astrolog-qt ] || { echo "build it first: make qt"; exit 2; }

ver=$(tools/ci-assert-version.sh)
app="$out/Astrolog.app"
rm -rf "$out"; mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"

echo "== staging $ver"
cp astrolog-qt "$app/Contents/MacOS/Astrolog"
cp -R ephem font icons "$app/Contents/MacOS/"
cp astrolog.as atlas.as timezone.as sefstars.txt seorbel.txt astexo.csv \
   earth.bmp "$app/Contents/MacOS/"

# The icon. The largest source art in this tree is 48x48, so every size
# above that is an upscale and will look soft next to a modern app icon.
# Saying so beats pretending otherwise; replacing icons/ with real 1024px
# art is the fix, and it is art work rather than build work.
iconset="$out/Astrolog.iconset"; mkdir -p "$iconset"
cp icons/astrolog16.png "$iconset/icon_16x16.png"
cp icons/astrolog32.png "$iconset/icon_16x16@2x.png"
cp icons/astrolog32.png "$iconset/icon_32x32.png"
for s in 64 128 256 512 1024; do
  sips -z $s $s icons/astrolog48.png --out "$iconset/tmp$s.png" >/dev/null 2>&1
done
mv "$iconset/tmp64.png"   "$iconset/icon_32x32@2x.png"
mv "$iconset/tmp128.png"  "$iconset/icon_128x128.png"
cp "$iconset/tmp256.png"  "$iconset/icon_128x128@2x.png"
mv "$iconset/tmp256.png"  "$iconset/icon_256x256.png"
cp "$iconset/tmp512.png"  "$iconset/icon_256x256@2x.png"
mv "$iconset/tmp512.png"  "$iconset/icon_512x512.png"
mv "$iconset/tmp1024.png" "$iconset/icon_512x512@2x.png"
iconutil -c icns "$iconset" -o "$app/Contents/Resources/Astrolog.icns"
rm -rf "$iconset"

cat > "$app/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key>            <string>Astrolog</string>
  <key>CFBundleIdentifier</key>            <string>org.astrolog.qt</string>
  <key>CFBundleName</key>                  <string>Astrolog</string>
  <key>CFBundleDisplayName</key>           <string>Astrolog</string>
  <key>CFBundleIconFile</key>              <string>Astrolog</string>
  <key>CFBundlePackageType</key>           <string>APPL</string>
  <key>CFBundleShortVersionString</key>    <string>$ver</string>
  <key>CFBundleVersion</key>               <string>$ver</string>
  <key>LSMinimumSystemVersion</key>        <string>11.0</string>
  <key>NSHighResolutionCapable</key>       <true/>
  <key>NSHumanReadableCopyright</key>
    <string>Astrolog is free software under the GNU GPL v2 or later.</string>
</dict>
</plist>
PLIST
printf 'APPL????' > "$app/Contents/PkgInfo"

echo "== macdeployqt"
# Homebrew's Qt is keg-only and split across qtbase/qtsvg/..., and
# macdeployqt prints "Cannot resolve rpath" for every framework it cannot
# find that way -- QtPdf, QtSvg, QtVirtualKeyboard, libbrotli, libwebp.
# Astrolog links none of those; they are transitive references inside Qt's
# own plugins. The errors are noise on this Qt and the deploy still
# produces a working bundle, which the run below is what actually decides.
"$(brew --prefix qt)/bin/macdeployqt" "$app" -always-overwrite || true

echo "== ad-hoc signature"
codesign --force --deep --sign - "$app"
codesign --verify --deep --strict "$app" && echo "   signature verifies"

# Run it before shipping it. The binary is inside the bundle now, with
# macdeployqt's rewritten library paths, which is the arrangement that
# actually gets downloaded -- and the one a plain "make qt" never tests.
echo "== what is actually in the bundle"
printf '   MacOS:     '; ls "$app/Contents/MacOS" | tr '\n' ' '; echo
printf '   ephem:     '; ls "$app/Contents/MacOS/ephem" 2>/dev/null | wc -l | tr -d ' '
printf ' files\n   Resources: '; ls "$app/Contents/Resources" | tr '\n' ' '; echo

echo "== does the bundled binary compute?"
# stderr is kept and PRINTED ON EVERY FAILURE PATH, not only the missing
# -line one. The first version showed it only when Chiron was absent, so
# a Chiron reading 0Ari00 -- the ephemeris-not-found case, which is the
# failure this is most likely to hit -- reported the symptom and threw
# away Astrolog's own "not found in PATH '...'" line naming what it
# searched. That is the fourth time in this project a check has discarded
# the evidence it existed to collect.
QT_QPA_PLATFORM=offscreen "$app/Contents/MacOS/Astrolog" \
  -Yi1 "$app/Contents/MacOS/ephem" \
  -qa 6 15 1990 12:00 0 122W19 47N36 -R1 _X -os "$out/chart.txt" \
  >"$out/run.out" 2>&1 || true
fail=""
grep -q '^Chir' "$out/chart.txt" 2>/dev/null || fail="no Chiron line"
grep -q "0Ari00" "$out/chart.txt" 2>/dev/null && fail="ephemeris not found"
if [ -n "$fail" ]; then
  echo "   FAILED: $fail"
  echo "   --- the program said ---"
  sed 's/^/   /' "$out/run.out" | head -10
  echo "   --- chart.txt, first 6 ---"
  sed 's/^/   /' "$out/chart.txt" 2>/dev/null | head -6
  exit 1
fi
grep '^Chir' "$out/chart.txt" | head -1 | sed 's/^/   /'
rm -f "$out/chart.txt" "$out/run.out"

echo "== dmg"
hdiutil create -volname "Astrolog $ver" -srcfolder "$app" \
  -ov -quiet -format UDZO "$out/astrolog-$ver-macos.dmg"
ls -lh "$out/astrolog-$ver-macos.dmg" | awk '{print "   "$5"  "$9}'
