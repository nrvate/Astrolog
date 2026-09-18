#!/bin/sh
# The Qt APPLICATION casting through a v4 ephemeris server, headless.
#
# Renders the SAME chart twice -- once from the local Swiss files, once
# through astrolog-ephd over a WebSocket -- and compares the two windows
# pixel for pixel. The server's own log says what it was asked and how long
# it took. Everything started here is stopped here, by the PID started.
set -u
W=${APPDEMO_OUT:-/nvm/work/appdemo}; rm -rf "$W"; mkdir -p "$W"
DISP=${APPDEMO_DISPLAY:-:77}
PORT=${APPDEMO_PORT:-47396}
CHART="-qa 1 1 2000 12:00 0 0e0 0n0"

Xvfb $DISP -screen 0 1280x900x24 >"$W/xvfb.log" 2>&1 &
PX=$!
./astrolog-ephd --bind 127.0.0.1 --port $PORT --threads 1 \
  --ephe "$PWD/ephem;$PWD" --log-contents --log-level debug >"$W/daemon.log" 2>&1 &
PS=$!
PA=""
cleanup() { [ -n "$PA" ] && kill $PA 2>/dev/null; kill $PS 2>/dev/null; kill $PX 2>/dev/null; }
trap cleanup EXIT INT TERM

# Wait for the DISPLAY before launching anything onto it: without this the
# app starts, finds no X server, and exits silently with an empty log --
# which reads exactly like "the app is broken" and is not.
i=0; while [ $i -lt 100 ]; do
  DISPLAY=$DISP xdpyinfo >/dev/null 2>&1 && break
  i=$((i+1)); sleep 0.2
done
if ! DISPLAY=$DISP xdpyinfo >/dev/null 2>&1; then
  echo "Xvfb never came up on $DISP"; cat "$W/xvfb.log"; exit 1
fi
i=0; while [ $i -lt 100 ]; do
  ./eph_wsclient --host 127.0.0.1 --port $PORT --quiet --jd 2451545.0 --meta --objs 4 2>/dev/null | grep -q "err=0" && break
  i=$((i+1)); sleep 0.2
done

shot() {   # shot <tag> <source-args...>
  tag=$1; shift
  DISPLAY=$DISP ./astrolog-qt -Yi0 "$PWD/ephem" "$@" -X $CHART >"$W/app-$tag.log" 2>&1 &
  PA=$!
  # The ROOT window of this private Xvfb. Matching by pid proved fragile:
  # Qt does not reliably set _NET_WM_PID before the first paint, so the
  # search returned nothing while the app was plainly running and drawing,
  # and "import -window ''" then failed silently into /dev/null. Nothing
  # else is on this display, so root is the app.
  sleep 16
  DISPLAY=$DISP import -window root "$W/$tag.png" 2>"$W/$tag.err" \
    || echo "  import failed for $tag: $(head -1 "$W/$tag.err")"
  kill $PA 2>/dev/null; wait $PA 2>/dev/null; PA=""
  sleep 1
}

nReq0=$(grep -c "evt=req " "$W/daemon.log")
shot local -bE swiss
shot server -bE server -bP server.url "ws://127.0.0.1:$PORT"
nReq1=$(grep -c "evt=req " "$W/daemon.log")

echo "=== the two renders"
python3 - "$W/local.png" "$W/server.png" <<'PY'
import sys
from PIL import Image, ImageChops
a=Image.open(sys.argv[1]).convert("RGB"); b=Image.open(sys.argv[2]).convert("RGB")
if a.size!=b.size:
    print("  sizes differ:",a.size,b.size); sys.exit()
d=ImageChops.difference(a,b); bb=d.getbbox()
n=sum(1 for p in d.getdata() if p!=(0,0,0))
print("  local  %s" % (a.size,))
print("  server %s" % (b.size,))
print("  differing pixels: %d of %d  %s" % (n, a.size[0]*a.size[1],
      "-- PIXEL IDENTICAL" if n==0 else "(bbox %s)" % (bb,)))
PY
echo "=== what the server was asked (requests $nReq0 -> $nReq1)"
grep "evt=req " "$W/daemon.log" | grep 'client' >/dev/null 2>&1
grep "evt=req " "$W/daemon.log" | tail -3 | sed -E 's/.*req=([0-9]+) objs=([0-9]+).*compute_ms=([0-9.]+) total_ms=([0-9.]+).*bodies=(.*)/  req \1: \2 objects, compute \3 ms, total \4 ms\n    bodies: \5/'
echo "=== the app's own output"
for t in local server; do printf "  %-7s " "$t"; head -1 "$W/app-$t.log" 2>/dev/null | cut -c1-70; echo; done
