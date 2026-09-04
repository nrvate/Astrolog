#!/bin/sh
# Compare Astrolog's planet longitudes against Swiss Ephemeris's OWN
# swetest binary, built from a separate source tree.
#
#   tools/swetest-oracle.sh [astrolog] [swetest] [ephemeris-dir]
#
# THE ONLY CHECK HERE THAT COMES FROM OUTSIDE THIS REPOSITORY.
# Every harness in this project compares Astrolog to an older build of
# itself, or to the other build of the same core; REFACTORING.md T9 says
# so, and adds that a differential "actively protects a wrong answer,
# because fixing a 30-year-old bug shows up as a regression".
#
# qttest.cpp's numeric oracle is the closest thing to an exception, and it
# is not one: it links the SAME vendored Swiss that Astrolog computes
# with, so it validates the object mapping rather than the arithmetic. If
# swemplan.cpp returned a wrong Jupiter, both sides of that comparison
# would return it together.
#
# swetest is a different binary from a different source tree, built from
# UPSTREAM Swiss Ephemeris -- aloistr/swisseph, SE_VERSION "2.10.03",
# which is exactly the version this tree vendors. That matters: the
# reference should be the canonical source at the same version, not
# somebody's patched fork, so that a disagreement means "Astrolog's
# integration is wrong" rather than "the two Swisses differ".
#
# UPSTREAM SILENTLY FALLS BACK TO MOSHIER when the .se1 files are
# missing, which would compare Moshier against Swiss and call it a
# result. The difference is small enough to look like rounding -- Sun on
# 15.6.1990 is 84 deg 7'46.3995 from Swiss and 84 deg 7'46.4407 from
# Moshier, 0.04 arcsec apart -- so this checks the ephemeris files are
# there before trusting anything.
#
# IT IS a CI check, in the slow lane, which was not obvious at first: the
# fork is a public repository and a shallow clone plus "make swetest"
# is 25 seconds measured (22 to clone, 2.8 to build), against the bundled
# 12-file ephem/ rather than the 887,000-file collection. Run it by hand
# too when a change touches calc.cpp, matrix.cpp or anything under swe*,
# because a release gate is a slow way to learn you broke an ephemeris.
set -e

A=${1:-./astrolog}
T=${2:-/shares/swisseph/bin/swetest}
E=${3:-/swe}
[ -x "$A" ] || { echo "no astrolog at $A"; exit 2; }
[ -x "$T" ] || { echo "no swetest at $T -- build one in a swisseph checkout"; exit 2; }
[ -d "$E" ] || { echo "no ephemeris directory at $E"; exit 2; }
# The three files the ten planets need. Without them upstream swetest
# answers from Moshier without saying so, and the comparison becomes
# meaningless while still looking like a pass.
for f in sepl_18.se1 semo_18.se1 seas_18.se1; do
  [ -f "$E/$f" ] || { echo "$E has no $f -- swetest would silently answer"
                      echo "from Moshier and this comparison would prove nothing."
                      exit 2; }
done

# TWO PARSING TRAPS, both of which read as Astrolog computing a wrong
# number before they were understood:
#   - swetest prints the angle as ONE field when minutes >= 10
#     ("188<deg>59'39.4465") and TWO when padded ("278<deg>" " 9'54.7633").
#   - Astrolog prints "Sun : 84.130" with the colon separate for
#     three-letter names, and "Merc: 65.691" without.
worst=0; worstwhat=""; n=0
for date in "15.6.1990" "1.1.2000" "4.7.1976" "29.2.2024" "21.12.2012"; do
  ad=$(echo "$date" | awk -F. '{printf "%s %s %s", $2, $1, $3}')
  sw=$("$T" -p0123456789 -b"$date" -ut12:00 -fPL -eswe -edir"$E" 2>/dev/null \
       | awk '/^(Sun|Moon|Mercury|Venus|Mars|Jupiter|Saturn|Uranus|Neptune|Pluto)/{
           v=""; for(i=2;i<=NF;i++) v=v $i;
           gsub(/\xc2\xb0/," ",v); gsub(/\x27/," ",v); split(v,p," ");
           printf "%s %.4f\n", $1, p[1]+p[2]/60+p[3]/3600}')
  as=$("$A" -Yi1 "$E" -sd -qa $ad 12:00 0 0:00E 0:00N -R1 _X 2>/dev/null \
       | sed -n 's/^\(Sun\|Moon\|Merc\|Venu\|Mars\|Jupi\|Satu\|Uran\|Nept\|Plut\) *: *\([0-9.]*\).*/\1 \2/p')
  i=0
  for name in Sun Moon Mercury Venus Mars Jupiter Saturn Uranus Neptune Pluto; do
    i=$((i+1))
    s=$(printf '%s\n' "$sw" | awk -v x="$name" '$1==x{print $2; exit}')
    a=$(printf '%s\n' "$as" | sed -n "${i}p" | awk '{print $2}')
    [ -n "$s" ] && [ -n "$a" ] || { echo "  PARSE FAILED for $name on $date"; exit 1; }
    n=$((n+1))
    d=$(awk -v x="$s" -v y="$a" 'BEGIN{d=x-y; if(d<0)d=-d; if(d>180)d=360-d; printf "%.4f", d}')
    if [ "$(awk -v d="$d" -v w="$worst" 'BEGIN{print (d>w)?1:0}')" = 1 ]; then
      worst=$d; worstwhat="$name on $date (swetest $s, astrolog $a)"
    fi
  done
done

echo "  $n comparisons, worst deviation ${worst} degrees"
echo "  at: $worstwhat"
# 0.001 degrees is Astrolog's own display precision at -sd, so anything at
# or under it is rounding rather than disagreement.
if [ "$(awk -v w="$worst" 'BEGIN{print (w>0.001)?1:0}')" = 1 ]; then
  echo "  EXCEEDS Astrolog's 0.001-degree display precision -- a real difference."
  exit 1
fi
echo "  within display precision: the two agree"
