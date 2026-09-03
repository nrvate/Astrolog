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

# The minimum macOS this bundle can honestly claim, read out of the binary
# rather than asserted.
#
# Info.plist used to hardcode LSMinimumSystemVersion 11.0 while NOTHING in
# this tree set MACOSX_DEPLOYMENT_TARGET or -mmacosx-version-min. So the
# binary was built against whatever SDK the runner had -- macos-26-arm64
# today, with Homebrew Qt bottled for Tahoe -- and the bundle advertised
# 11.0 regardless. A bundle that promises an OS it cannot run on fails at
# launch with a message about the wrong thing.
#
# Rather than guess a target that Homebrew's Qt may or may not support,
# take what the linker actually recorded. LC_BUILD_VERSION carries "minos"
# on anything modern; LC_VERSION_MIN_MACOSX carried "version" on older
# toolchains, so both are tried. If neither is readable the old 11.0 is
# kept and said out loud, because silently stamping a guess is what this
# is fixing.
minos=$(otool -l ./astrolog-qt 2>/dev/null \
  | awk '/LC_BUILD_VERSION/,0' | awk '/minos/{print $2; exit}')
if [ -z "$minos" ]; then
  minos=$(otool -l ./astrolog-qt 2>/dev/null \
    | awk '/LC_VERSION_MIN_MACOSX/,0' | awk '/version/{print $2; exit}')
fi
if [ -z "$minos" ]; then
  echo "   WARNING: could not read a minimum OS from the binary;"
  echo "   LSMinimumSystemVersion stays 11.0 and is unverified."
  minos=11.0
else
  echo "   minimum macOS, read from the binary: $minos"
fi
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
  <key>LSMinimumSystemVersion</key>        <string>$minos</string>
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

# That count was printed and never checked. Check it: the esoteric-body
# files are most of the bundle's payload, and the Chiron assertion further
# down cannot see them go -- seas_18.se1, where Chiron lives, was in the
# original 12-file set and would still answer.
want_se1=$(ls ephem/*.se1 2>/dev/null | wc -l | tr -d ' ')
got_se1=$(ls "$app/Contents/MacOS/ephem"/*.se1 2>/dev/null | wc -l | tr -d ' ')
[ "$want_se1" -gt 0 ] && [ "$got_se1" = "$want_se1" ] || {
  echo "   FAILED: bundle has $got_se1 .se1 files, this tree ships $want_se1"
  exit 1; }
echo "   ephem complete: $got_se1 of $want_se1 .se1 files"

# Does it depend on anything that will not be on the target machine? This
# runs on a builder where Homebrew Qt exists, so a binary still linking
# /opt/homebrew loads perfectly here and fails on every Mac that has not
# installed Qt -- which is every Mac this .dmg is for. macdeployqt is
# supposed to rewrite those to @executable_path/../Frameworks, and it
# printed a page of "Cannot resolve rpath" errors doing it, so this is
# not a hypothetical.
#
# The Linux packages have had exactly this check since they existed:
# ci-verify-linux-package.sh runs ldd and greps for "not found". otool -L
# is the same question in the same spirit.
# Is it a valid BUNDLE, not merely a directory with a binary in it?
# Every other check here runs Contents/MacOS/Astrolog directly, which
# bypasses the bundle machinery completely -- a malformed Info.plist, or
# a CFBundleExecutable naming a file that is not there, passes all of
# them and fails the instant someone double-clicks. plutil and a couple
# of existence tests cost nothing and cover the one path a user takes
# that none of the rest of this script does.
echo "== is it a well-formed bundle?"
plutil -lint "$app/Contents/Info.plist" >/dev/null \
  || { echo "   Info.plist is not valid property list"; exit 1; }
exe=$(plutil -extract CFBundleExecutable raw "$app/Contents/Info.plist")
[ -x "$app/Contents/MacOS/$exe" ] \
  || { echo "   CFBundleExecutable is '$exe' and there is no such executable"; exit 1; }
icon=$(plutil -extract CFBundleIconFile raw "$app/Contents/Info.plist")
[ -f "$app/Contents/Resources/$icon.icns" ] \
  || { echo "   CFBundleIconFile is '$icon' and Resources/$icon.icns is missing"; exit 1; }
plv=$(plutil -extract CFBundleShortVersionString raw "$app/Contents/Info.plist")
[ "$plv" = "$ver" ] \
  || { echo "   Info.plist says version $plv, the build says $ver"; exit 1; }
echo "   Info.plist valid; executable '$exe', icon '$icon.icns', version $plv"

echo "== does it depend on anything outside the bundle?"
bad=$(otool -L "$app/Contents/MacOS/Astrolog" \
  | awk 'NR>1{print $1}' \
  | grep -E '^(/opt/homebrew|/usr/local|/opt/local)' || true)
if [ -n "$bad" ]; then
  echo "   FAILED: links libraries that will not exist on a user's Mac:"
  printf '%s\n' "$bad" | sed 's/^/     /'
  echo "   macdeployqt should have rewritten these to @executable_path."
  exit 1
fi
otool -L "$app/Contents/MacOS/Astrolog" | awk 'NR>1{print $1}' \
  | grep -c "@executable_path\|@rpath" | sed 's/^/   /' | tr -d '\n'
echo " bundled-relative references, none pointing at Homebrew"

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
# The CHIRON line, not the whole chart. -R1 renders 118 bodies including
# 32 planetary moons, whose files live in sat/ and are not in the bundled
# ephem/ at all -- so "does 0Ari00 appear anywhere" can never pass here,
# and failed v8.00-qt.5 on a bundle that was perfectly good. Chiron comes
# from seas_18.se1, which IS bundled, which is exactly why every other
# package check in this repository asserts on Chiron and nothing else.
fail=""
chir=$(grep '^Chir' "$out/chart.txt" 2>/dev/null | head -1)
[ -n "$chir" ] || fail="no Chiron line"
case $chir in *0Ari00*) fail="ephemeris not found: $chir" ;; esac
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
dmg="$out/astrolog-$ver-macos.dmg"
hdiutil create -volname "Astrolog $ver" -srcfolder "$app" \
  -ov -quiet -format UDZO "$dmg"
ls -lh "$dmg" | awk '{print "   "$5"  "$9}'

# Mount it and run the app from INSIDE, which is the only copy anyone
# will ever launch. Everything above tested the staging directory: a
# read-only compressed volume is a different filesystem with different
# permissions, and "it worked in out/macos" has never been the claim
# worth making. Same reasoning as installing every .deb and .rpm into a
# clean container rather than trusting dpkg-deb's exit code.
echo "== does the app inside the .dmg run?"
# hdiutil prints tab-separated columns: device, UUID, mount point. Take
# the LAST field, because a volume name contains spaces ("Astrolog
# 8.00-qt.6") and splitting on whitespace would cut it in half. The first
# attempt tried to strip everything before "/Volumes" with a substitution
# anchored on a run of non-slash characters, which matches nothing here
# because the line begins "/dev/disk32s1" -- so $vol held the whole line
# and the check reported "no Chiron line" about a path that could not
# exist.
vol=$(hdiutil attach "$dmg" -nobrowse -readonly | awk -F'\t' '/\/Volumes\//{print $NF; exit}')
[ -n "$vol" ] || { echo "   could not mount $dmg"; exit 1; }
trap 'hdiutil detach "$vol" -quiet 2>/dev/null || true' EXIT
"$vol/Astrolog.app/Contents/MacOS/Astrolog" \
  -Yi1 "$vol/Astrolog.app/Contents/MacOS/ephem" \
  -qa 6 15 1990 12:00 0 122W19 47N36 -R1 _X -os "$out/dmg.txt" \
  >"$out/dmg.out" 2>&1 || true
chir=$(grep '^Chir' "$out/dmg.txt" 2>/dev/null | head -1)
bad=""
[ -n "$chir" ] || bad="no Chiron line"
case $chir in *0Ari00*) bad="ephemeris not found" ;; esac
if [ -n "$bad" ]; then
  echo "   FAILED from the mounted volume $vol: $bad"
  sed 's/^/   /' "$out/dmg.out" | head -8
  exit 1
fi
grep '^Chir' "$out/dmg.txt" | head -1 | sed 's/^/   /'
codesign --verify --deep --strict "$vol/Astrolog.app" \
  && echo "   signature still verifies after packaging"
rm -f "$out/dmg.txt" "$out/dmg.out"
