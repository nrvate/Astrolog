#!/bin/sh
# Every "-W" spelling's argument count, on ONE COMMAND LINE.
#
#   tools/w-switch-arity.sh ./astrolog
#
# The arity of these is written out THREE TIMES and nothing compares the
# three: NProcessSwitchesQt() in qtdriver.cpp acts on them, and
# NProcessSwitchesNullW() in switch.cpp and NProcessSwitchesW() in
# wdriver.cpp consume them so that a shared astrolog.as still loads. Get
# one wrong and the switch AFTER it is read as its argument.
#
# ONE COMMAND LINE IS THE WHOLE POINT. A settings file cannot see an arity
# bug: each line is parsed on its own, so a switch that eats one argument
# too many runs off the end of its line and the next line starts clean.
# Two checks in this tree were first written the weak way and proved much
# less than they claimed.
#
# WHAT COVERS WHAT. The Qt consumer has the "interface-settings" group and
# the Win32 one has tools/scenarios/win-switch-arity.txt -- five spellings
# each. NProcessSwitchesNullW() had nothing, which is the one of the three
# with no behaviour to notice: it is a bare table of counts, so a wrong
# one is invisible until a user's settings file stops loading.
#
# HOW: run each spelling followed by "-YQ 47" and require that to reach
# the saved settings. Too many arguments consumed and the sentinel is
# eaten; too few and the leftover argument is read as a switch.
#
# WHICH IS WHY THE ARGUMENTS ARE THE NUMBERS THEY ARE, and this is the
# whole difficulty of the check. A leftover token only gives the game away
# if it fails to resolve, and most tokens resolve: "1", "10", "20", "50"
# and "0" are registry rows, and any WORD matches a prefix row -- "Monospace"
# resolves as "-M", "ProbeName" as "-P". A first draft used exactly those
# and passed with three arities deliberately broken. Every argument below
# is a number measured to be an unknown switch on its own, so the leftover
# is loud. The one that cannot be: "-WI" takes 0, 1 or 2, and all three
# resolve, so its too-few direction is not covered here -- the Qt suite's
# "interface-settings" group asserts that one by its effect instead.
#
# And the other way round, because a check that only ever passes proves
# nothing: a spelling given one argument too FEW must be refused.
set -u
B=${1:-./astrolog}
cd "$(dirname "$0")/.." || exit 1
[ -x "$B" ] || { echo "build it first: make"; exit 1; }
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fail=0
c=0

try() {
  sw=$1; shift
  c=$((c + 1))
  # Removed first, every time. A refused parse writes NOTHING, so without
  # this the previous run's file is still there and the grep below finds
  # its sentinel -- which is how the first draft of this passed with three
  # arities deliberately broken.
  rm -f "$T/o.as"
  env -u DISPLAY "$B" -n "$sw" "$@" -YQ 47 _X -od "$T/o.as" \
    </dev/null >/dev/null 2>"$T/err"
  if grep -q '^-YQ 47' "$T/o.as" 2>/dev/null; then
    return 0
  fi
  echo "  FAIL: \"$sw $*\" ate the switch after it, or was refused"
  sed 's/^/        /' "$T/err" | head -2
  fail=1
}

# Every spelling the three consumers name, with the arguments each wants.
try -W    25
try -WN   25
try -WM   1 47
try -WM0  0 47
try -WT   47
try -Ww   25 47
try -WB   25 47
try -Wx   6
try -WF   47 12
try -WG   47 12
try -WI   1
try =WFa
try =WGa
try -Wh
try -Wn
try -Wt
try -Wb
try -WZ
try -Wo
try -Wo0
try -WS

# The other direction: one argument short has to be refused, or the check
# above would pass on a switch that consumes nothing at all.
#
# AT THE END OF THE LINE, and that is not tidiness. "-WF 47 _X -od f" is
# NOT one argument short: "_X" becomes the size, NFromSz() reads it as 0,
# and 0 is a legal "no preference" -- so the switch swallows the graphics
# flag and everything still works. Only a spelling with nothing at all
# after it can be short.
rm -f "$T/o.as"
env -u DISPLAY "$B" -n _X -od "$T/o.as" -WF 47 \
  </dev/null >/dev/null 2>"$T/err"
if [ -s "$T/err" ] && ! [ -s "$T/o.as" ]; then
  c=$((c + 1))
else
  echo "  FAIL: \"-WF 47\" is one argument short and was accepted"
  fail=1
fi

if [ $fail = 0 ]; then
  echo "w-switch-arity: $c -W spellings consume what they should"
else
  echo "w-switch-arity: FAILED"
fi
exit $fail
