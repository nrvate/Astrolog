#!/bin/sh
# Every file in ephem/ is really a Swiss Ephemeris file.
#
#   tools/check-ephem.sh
#
# Swiss .se1 files begin with the literal "SWISSEPH". On 2026-09-05, four
# files copied from the maintainer's /swe mount into the bundle turned
# out to be 1,198-byte HTML 404 pages -- a download that had failed years
# earlier and saved the error page under the name it had asked for. They
# were named se00001s.se1 through se00004s.se1, for Ceres, Pallas, Juno
# and Vesta, and the program never complained: those four are computed
# from seas_18.se1 anyway, so nothing ever read them.
#
# That is the shape worth guarding against. A corrupt ephemeris file for
# a body nothing reads is invisible; for a body something reads it is a
# 0Ari00'00" position, which looks like an answer. Both cost nothing to
# rule out: 75 files, one read of eight bytes each.
set -eu
cd "$(dirname "$0")/.."
[ -d ephem ] || { echo "no ephem/ directory"; exit 2; }
bad=0; n=0
for f in ephem/*.se1; do
  n=$((n + 1))
  head -c 8 "$f" | grep -q '^SWISSEPH' || {
    echo "NOT AN EPHEMERIS FILE: $f ($(wc -c < "$f") bytes, starts \"$(head -c 16 "$f" | tr -c '[:print:]' '.')\")"
    bad=$((bad + 1)); }
done
[ "$n" -gt 0 ] || { echo "ephem/ holds no .se1 files at all"; exit 1; }
[ "$bad" -eq 0 ] || { echo "== $bad of $n files in ephem/ are not ephemeris data"; exit 1; }
echo "$n files, all Swiss Ephemeris data"
