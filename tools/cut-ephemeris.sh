#!/bin/sh
# Cut a span of JPL's DE441 and of its SB441-N16 asteroid kernel for the
# Prometheia source -- the way the 1900-2100 pair shipped in ephem/ was cut.
#
#   tools/cut-ephemeris.sh <from-year> <to-year> --de441 <file> --sb441 <file> [--out <dir>]
#   tools/cut-ephemeris.sh <from-year> <to-year> --fetch <dir> [--out <dir>]
#
# Writes two files into --out (default ephem/):
#   linux_<span>.441          the planets, Moon and Sun, from DE441
#   sb441-<span>.bsp          the 16 most massive asteroids' paths (JPL's
#                             SB441-N16), which the catalog bodies are
#                             integrated against
# <span> is named the way JPL and Prometheia name one: p for a year from 0
# on, m before it, so 1900 to 2100 is p1900p2100 and -3000 to 3000 is
# m3000p3000.
#
# The cut runs from 1 January of <from-year> to 1 January of <to-year>, 0h
# TDB, widened to the source's whole records (32 days for DE441, whole
# segments for the kernel), and every state inside it is the source's to
# the bit (Prometheia's docs/DE.md, "Cutting a span").
#
# --fetch runs Prometheia's own fetcher, which downloads and verifies the
# full files into <dir> once: about 2.8 GB of DE441 and 616 MB of the
# kernel. Or name files you already have with --de441 and --sb441.
#
# To use a cut: Ephemeris Settings, Prometheia, "Planets" for the years
# you cast most, or "Planets, Other Years" for a second file consulted
# outside the first one's span (with its kernel as "Asteroid perturbers").
# A date outside every file falls back to the Swiss Ephemeris.
set -eu
cd "$(dirname "$0")/.."

usage() {
  echo "usage: tools/cut-ephemeris.sh <from-year> <to-year> (--de441 FILE --sb441 FILE | --fetch DIR) [--out DIR]" >&2
  exit 2
}
[ $# -ge 2 ] || usage
y0=$1; y1=$2; shift 2
case $y0$y1 in *[!0-9-]*|'') usage ;; esac
[ "$y0" -lt "$y1" ] || { echo "cut-ephemeris: <from-year> must be before <to-year>" >&2; exit 2; }
de=; sb=; fetch=; out=ephem
while [ $# -gt 0 ]; do
  case $1 in
    --de441) de=${2:?}; shift 2 ;;
    --sb441) sb=${2:?}; shift 2 ;;
    --fetch) fetch=${2:?}; shift 2 ;;
    --out)   out=${2:?}; shift 2 ;;
    *) usage ;;
  esac
done

if [ -n "$fetch" ]; then
  echo "== fetching the full DE441 and SB441-N16 into $fetch (about 3.4 GB, once)" >&2
  python3 prometheia/tools/fetch/de_fetch.py --dir "$fetch" --only de441 --only sb441 >&2
  de=${de:-$fetch/linux_m13000p17000.441}
  sb=${sb:-$fetch/sb441-n16.bsp}
fi
[ -n "$de" ] && [ -n "$sb" ] || usage
for f in "$de" "$sb"; do
  [ -f "$f" ] || { echo "cut-ephemeris: $f is not there" >&2; exit 1; }
done

# The Julian day of 1 January, 0h, of a proleptic Gregorian year, which
# may be negative; Python's floor division gets those right.
jd() {
  python3 -c "
y = $1 + 4800
print('%.1f' % (1 + (153 * 10 + 2) // 5 + 365 * (y - 1) + (y - 1) // 4 - (y - 1) // 100 + (y - 1) // 400 - 32045 - 0.5))"
}
name() { case $1 in -*) echo "m${1#-}" ;; *) echo "p$1" ;; esac; }
span=$(name "$y0")$(name "$y1")
j0=$(jd "$y0"); j1=$(jd "$y1")

mkdir -p "$out"
detrim=$(tools/prometheia-tool.sh de-trim)
spktrim=$(tools/prometheia-tool.sh spk-trim)
echo "== $y0 to $y1: JD $j0 to $j1" >&2
"$detrim" "$de" "$out/linux_$span.441" --from "$j0" --to "$j1"
"$spktrim" "$sb" "$out/sb441-$span.bsp" --from "$j0" --to "$j1"
ls -l "$out/linux_$span.441" "$out/sb441-$span.bsp"
