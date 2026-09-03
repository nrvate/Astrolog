#!/bin/sh
# Install the Windows installer under Wine, check what it left, uninstall
# it, and check that it left nothing.
#
#   tools/ci-verify-windows-installer.sh <setup.exe> <staged-dir>
#
# The Linux packages have been installed into clean containers and run
# from "/" since they existed. The Windows artifact was verified
# structurally -- the right files present, no sources, a SHA256SUMS -- and
# never actually installed. An installer has more to get wrong than a zip
# does: a wrong InstallDir, a File /r that misses a subdirectory, an
# uninstaller that removes the shortcut and leaves 40 MB of ephemeris.
#
# NSIS supports /S for silent and /D= for the directory, so all of that is
# checkable without a Windows machine. A throwaway WINEPREFIX keeps it
# away from any Wine setup the developer already has.
#
# WINE IS NOT A SECOND-BEST HERE, IT IS THE ONLY AUTOMATABLE OPTION.
# tools/astrolog.nsi declares RequestExecutionLevel admin, because it
# installs to Program Files and writes HKLM. On real Windows that means
# UAC, and UAC cannot prompt a non-interactive session: running this
# installer through VBoxManage guestcontrol on a Windows 10 VM hung for a
# full ten-minute timeout, and the guest was left with consent.exe --
# the elevation prompt itself -- sitting there waiting for an answer that
# could never arrive. It could not even be killed from that session.
#
# So: Wine for the automated check, which exercises the file layout, the
# uninstaller and the payload comparison, and a human double-click for
# the elevation path. That is the same shape as the macOS Gatekeeper
# note -- the part a machine can check, checked; the part that needs a
# person, said out loud instead of faked.
set -e

setup=${1:?usage: ci-verify-windows-installer.sh <setup.exe> <staged-dir>}
staged=${2:?usage: ci-verify-windows-installer.sh <setup.exe> <staged-dir>}
[ -f "$setup" ] || { echo "no such installer: $setup"; exit 2; }
[ -d "$staged" ] || { echo "no such staged directory: $staged"; exit 2; }
command -v wine >/dev/null || { echo "wine not found"; exit 2; }

WINEPREFIX=$(mktemp -d)/pfx
export WINEPREFIX WINEDEBUG=-all
trap 'rm -rf "$(dirname "$WINEPREFIX")"' EXIT
mkdir -p "$WINEPREFIX"
timeout 300 wineboot -i >/dev/null 2>&1 || true

dst="$WINEPREFIX/drive_c/astrotest"
echo "== installing silently"
timeout 600 wine "$(cd "$(dirname "$setup")" && pwd)/$(basename "$setup")" \
  /S '/D=C:\astrotest' >/dev/null 2>&1 || true
[ -d "$dst" ] || { echo "   NOTHING INSTALLED into C:\\astrotest"; exit 1; }
echo "   $(find "$dst" -type f | wc -l | tr -d ' ') files"

echo "== is everything the package staged actually there?"
# Temp files rather than "diff <(...) <(...)". Process substitution is a
# bash extension and this script says #!/bin/sh, which on Debian is dash:
# the first version parsed fine to the eye, ran the install, and then died
# on "Syntax error: "(" unexpected" halfway through.
a=$(mktemp); b=$(mktemp)
( cd "$staged" && find . -type f | sort ) > "$a"
( cd "$dst" && find . -type f ! -name uninstall.exe | sort ) > "$b"
if ! diff "$a" "$b" > "$a.diff"; then
  echo "   the installed tree differs from the staged one:"
  head -12 "$a.diff" | sed 's/^/     /'
  rm -f "$a" "$b" "$a.diff"; exit 1
fi
rm -f "$a" "$b" "$a.diff"
echo "   identical to the staged payload"

[ -f "$dst/uninstall.exe" ] || { echo "   NO UNINSTALLER"; exit 1; }
echo "   uninstall.exe present"

echo "== uninstalling silently"
timeout 600 wine "$dst/uninstall.exe" /S >/dev/null 2>&1 || true
sleep 3
if [ -d "$dst" ]; then
  left=$(find "$dst" -type f 2>/dev/null | wc -l | tr -d ' ')
  [ "$left" -eq 0 ] || {
    echo "   uninstall left $left files behind:"
    find "$dst" -type f | head -8 | sed 's/^/     /'; exit 1; }
fi
echo "   removed cleanly"
