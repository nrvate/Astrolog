#!/bin/sh
# Run every Windows-build scenario, and report once.
#
#   tools/win-tests.sh                    # all of tools/scenarios/win-*.txt
#   tools/win-tests.sh win-objectsel      # just one
#
# The Qt build has thousands of in-process assertions -- run it and it
# prints the count; do not restate it here, since the three documents that
# did produced three different wrong answers. The Windows build had none
# at all, which stopped mattering the moment features started shipping in
# both. A Windows-side regression is otherwise invisible until somebody
# opens the dialog by hand.
#
# It cannot be done the way the Qt suite is. astrolog.exe is a GUI program
# with no console mode -- "wine astrolog.exe _X" just opens a window and
# sits there -- so there is nowhere to put an in-process test. Everything
# here therefore goes through tools/windrive.sh and a real window, which is
# slow (tens of seconds per scenario) and is why this is a separate script
# rather than part of the pre-commit checks.
#
# What it is actually good for: the Windows half of anything this fork adds
# to both builds. The shared logic underneath is already covered by the Qt
# suite, since both builds call the same calc.cpp and io.cpp.
set -e
cd "$(dirname "$0")/.." || exit 1
[ -f ./astrolog.exe ] || { echo "build it first: make -f Makefile.win"; exit 1; }

if [ -n "$1" ]; then
  set -- "tools/scenarios/$1.txt"
else
  set -- tools/scenarios/win-*.txt
fi

# Run against the real settings file, but not the real ephemeris path.
# nrvate.as carries -Yi "/swe", which holds ~887,000 .se1 files. Native
# Linux handles that; Wine's path translation does not, and the Windows
# build sits there long enough to look completely broken -- menu bar
# painted, client area black, every keystroke ignored. It is not a hang
# and not an app bug, it is the file count seen through Wine, and it is
# indistinguishable from the modal-dialog failure this repo has already
# been burned by. Swapping in the small bundled ephem/ keeps everything
# that makes this config worth testing against -- restrictions, orbs,
# aspect set, macros, window size, graphics mode -- and drops only the
# part Wine cannot traverse. The Qt suite still runs the true path.
CFG=$(mktemp /tmp/winascfg.XXXXXX.as)
sed 's|"/swe"|"ephem"|' nrvate.as > "$CFG"
trap 'rm -f "$CFG"' EXIT INT TERM

pass=0; fail=0; failed=""
for scn in "$@"; do
  [ -f "$scn" ] || { echo "no such scenario: $scn"; exit 2; }
  name=$(basename "$scn" .txt)
  printf '== %s ==\n' "$name"
  # Not a pipeline: piping into sed would report sed's exit status, and
  # every scenario would "pass".
  out=$(mktemp)
  if tools/windrive.sh run "$scn" --args "-i $CFG" >"$out" 2>&1; then
    pass=$((pass+1))
  else
    fail=$((fail+1)); failed="$failed $name"
  fi
  sed 's/^/  /' "$out"; rm -f "$out"
done

echo
if [ $fail -eq 0 ]; then
  echo "PASS: $pass scenario(s)"
else
  echo "FAIL:$failed ($fail of $((pass+fail)) scenario(s))"
fi
exit $([ $fail -eq 0 ] && echo 0 || echo 1)
