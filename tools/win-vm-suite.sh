#!/bin/sh
# Run the Qt suite inside a VirtualBox Windows VM and report the result.
#
#   tools/win-vm-suite.sh <vm> <dist-dir> [ephemeris-path]
#
# <dist-dir> is the astrolog-Qt6-windows-msvc artifact from nightly.yml,
# unpacked. The default ephemeris path is the bundled ephem\ beside the
# binary, which is complete enough for every group except the esoteric
# bodies -- pass a full ephemeris directory to cover those.
#
# Credentials come from WINVM_USER and WINVM_PASS, not from arguments,
# so they stay out of shell history and process lists.
#
# Four things about this are not obvious, and each cost a run to find:
#
#   1. No network is needed or used. VBoxManage guestcontrol rides the
#      Guest Additions channel, so a VM on an internal network with no
#      DHCP is fine. Guest Additions must be running (RunLevel >= 2).
#
#   2. QT_QPA_PLATFORM=offscreen is required, and the plugin has to be
#      in the artifact. windeployqt ships only "windows"; nightly.yml
#      copies qoffscreen.dll in beside it.
#
#   3. Do NOT fall back to the windows plugin. A guestcontrol process
#      runs in session 0, which has a window station but no desktop, so
#      the first widget blocks forever -- ten minutes, then a process to
#      kill by PID.
#
#   4. Point the ephemeris at a LOCAL disk, never a VirtualBox shared
#      folder. A negative lookup over vboxsf in a directory of 887,000
#      files was measured at 1.9 s against 90 ms for a hit, which turns
#      a 0.3 s chart into 32 s and looks exactly like a hang.
set -e

vm=$1
dist=$2
ephem=${3:-ephem}
[ -n "$vm" ] && [ -n "$dist" ] || {
  echo "usage: $0 <vm> <dist-dir> [ephemeris-path]"; exit 2; }
[ -f "$dist/astrolog-qt-test.exe" ] || {
  echo "no astrolog-qt-test.exe in $dist"; exit 2; }
[ -n "$WINVM_USER" ] && [ -n "$WINVM_PASS" ] || {
  echo "set WINVM_USER and WINVM_PASS"; exit 2; }

G="VBoxManage guestcontrol $vm --username $WINVM_USER --password $WINVM_PASS"

rl=$(VBoxManage showvminfo "$vm" --machinereadable 2>/dev/null |
  sed -n 's/^GuestAdditionsRunLevel=//p')
[ "${rl:-0}" -ge 2 ] || {
  echo "Guest Additions not up on $vm (runlevel ${rl:-none}). Is it running?"
  exit 1; }

# printf, not echo: a "\a" in a path is a bell to some shells, and
# "C:\astroqt" came out as "C:stroqt" the first time this ran.
printf '== copying %s files to C:\\astroqt\n' "$(find "$dist" -type f | wc -l)"
$G mkdir --parents 'C:\astroqt' >/dev/null 2>&1 || true
# A "find | while read" loop puts the body in a subshell, where "exit"
# cannot fail the script -- so the list goes to a file and the loop reads
# that instead. The first version copied nothing and carried on happily.
srcs=$(mktemp)
(cd "$dist" && find . -type f | sed 's|^\./||') > "$srcs"
absdist=$(cd "$dist" && pwd)
while read -r f; do
  d=$(dirname "$f")
  [ "$d" != "." ] &&
    $G mkdir --parents "C:\\astroqt\\$(printf '%s' "$d" | tr / '\\')" >/dev/null 2>&1
  if ! $G copyto \
      --target-directory "C:\\astroqt\\$(printf '%s' "$f" | tr / '\\')" \
      "$absdist/$f" >/dev/null 2>&1; then
    printf '  FAILED to copy %s\n' "$f"; rm -f "$srcs"; exit 1
  fi
done < "$srcs"
rm -f "$srcs"

echo "== running the suite (offscreen, ephemeris $ephem)"
$G run --exe 'C:\Windows\System32\cmd.exe' --wait-stdout --wait-stderr -- \
  cmd /c "cd C:\\astroqt && set QT_QPA_PLATFORM=offscreen&& \
    astrolog-qt-test.exe -Yi1 $ephem" > "${WINVM_LOG:-win-suite.log}" 2>&1 || true

log=${WINVM_LOG:-win-suite.log}
tail -40 "$log"
if grep -q '^PASS:' "$log"; then
  echo "== $vm: $(grep -h '^PASS:' "$log" | tail -1)"
  exit 0
fi
echo "== $vm: the suite did not report PASS. Full log in $log"
grep -q 'platform plugin' "$log" &&
  echo "== the offscreen plugin is missing from the artifact (see note 2)"
exit 1
