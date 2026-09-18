#!/bin/sh
# tools/node-frame-probe.sh -- does a node FOLLOW the frame rotation?
#
# Run by hand. It answers the question EPHEMERIS_ACCURACY_REGISTRY.md 4.2
# got wrong for a week: this server was believed to compute a node against
# the ecliptic of date and rotate it into the requested frame, and the
# latitudes it returned in a J2000 frame were small enough to look like
# that. They are not.
#
# The method needs no second engine and no precession routine. Every
# direction at one instant takes the SAME rotation from the mean ecliptic
# of date to J2000, so sampling a handful of ordinary bodies in both
# frames over-determines that rotation -- it fits to rms 0.00000 arcsec --
# and the node either follows it or it does not. Ours follows it in
# longitude, to 0.003 arcsec, and not in latitude, by tens of arcsec and
# at 2100 not even in sign.
#
#   ./astrolog-ephd --bind 127.0.0.1 --port 47415 --ephe "$PWD/ephem;$PWD" &
#   tools/node-frame-probe.sh | python3 tools/node-frame-fit.py
#
set -u
W=/nvm/work/np.$$; mkdir -p "$W"
./astrolog-ephd --bind 127.0.0.1 --port 47415 --threads 1 --ephe "$PWD/ephem;$PWD" > "$W/d.log" 2>&1 &
P=$!; trap 'kill $P 2>/dev/null; rm -rf "$W"' EXIT
i=0; while [ $i -lt 100 ]; do
  ./eph_wsclient --host 127.0.0.1 --port 47415 --quiet --jd 2451545.0 --meta --objs 4 2>/dev/null | grep -q "err=0" && break
  i=$((i+1)); sleep 0.2
done
for jd in 2415020.5 2378496.5 2488070.0; do
 for fr in mod j2000; do
  for o in 10 301 199 299 499 599 699; do
    printf "B %s %s %s " "$jd" "$fr" "$o"
    ./eph_wsclient --host 127.0.0.1 --port 47415 --quiet --jd "$jd" --count 1 --out /dev/stdout \
      --profile "frame=$fr,plane=ecl,corr=0" --objs "$o" 2>&1 | grep -v "^META" | head -1 | awk '{print $6, $7}'
  done
  printf "N %s %s node " "$jd" "$fr"
  ./eph_wsclient --host 127.0.0.1 --port 47415 --quiet --jd "$jd" --count 1 --out /dev/stdout \
    --profile "frame=$fr,plane=ecl,corr=0" --points "301:0:0" 2>&1 | grep -v "^META" | head -1 | awk '{print $6, $7}'
 done
done
