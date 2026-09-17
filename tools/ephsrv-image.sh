#!/bin/bash
# tools/ephsrv-image.sh -- build the astrolog-ephd container image, and
# prove it.
#
#   tools/ephsrv-image.sh [COMMIT]      # default HEAD; tags astrolog-ephd:<sha>
#
# The build context is a git archive of COMMIT, not the working tree, so
# nothing built or edited here can leak in (the same reasoning as
# tools/build-check.sh). Then the image is started over the bundled ephem/
# and asked, and must:
#   - report healthy (/healthz, and Docker's own HEALTHCHECK)
#   - run as a user other than root
#   - answer a window like a server built on this machine against the same
#     pinned fork (tools/ephsrv-fork.sh), over the wire: every name and flag
#     exact, positions within 1e-10 degrees, speeds within 1e-8 degrees a
#     day. NOT bit-exact, measured: the image's gcc 12 and glibc 2.36
#     against this machine's gcc 11 and glibc 2.35 moved 66 of 4801 rows,
#     by at most 1.3e-14 degrees and 6.5e-11 degrees a day -- libm and
#     code-generation rounding, the same class the fork's cross-toolchain
#     gate allows 1e-5 for. Bit-exactness holds within one toolchain, which
#     is what tools/ephsrv-golden.sh asks.
#   - raise its open-file limit past the container's soft 1024
#   - stop on `docker stop` by draining, exit code 0
#
# Needs docker. Minutes on a cold cache (two Debian images, the fork, the
# server); seconds warm. PORT: default 26900 + pid % 90, and PORT + 1.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
COMMIT=$(git rev-parse --verify "${1:-HEAD}^{commit}")
TAG="astrolog-ephd:${COMMIT:0:12}"
PORT=${PORT:-$((26900 + $$ % 90))}
SCRATCH=$(mktemp -d /nvm/work/ephsrv-image.XXXXXX 2>/dev/null || mktemp -d)
CID= HOST_PID=

cleanup() {
  [ -n "$CID" ] && { docker rm -f "$CID" > /dev/null 2>&1 || true; }
  [ -n "$HOST_PID" ] && { kill "$HOST_PID" 2>/dev/null || true; }
  rm -rf "$SCRATCH"
  true
}
trap cleanup EXIT
fail() { echo "IMAGE FAIL: $*"; [ -n "$CID" ] && docker logs "$CID" 2>&1 | tail -8 | sed 's/^/  ephd: /'; exit 1; }

command -v docker > /dev/null || fail "no docker"
echo "== context: git archive ${COMMIT:0:12}"
mkdir "$SCRATCH/ctx"
git archive "$COMMIT" | tar -x -C "$SCRATCH/ctx"
[ -f "$SCRATCH/ctx/ephsrv/deploy/Dockerfile" ] || fail "${COMMIT:0:12} has no ephsrv/deploy/Dockerfile"
docker build -q -t "$TAG" -f "$SCRATCH/ctx/ephsrv/deploy/Dockerfile" "$SCRATCH/ctx" > "$SCRATCH/build.log" 2>&1 ||
  { tail -30 "$SCRATCH/build.log"; fail "docker build"; }
echo "== built $TAG ($(docker image inspect -f '{{.Size}}' "$TAG" | awk '{printf "%.0f MB", $1/1e6}'))"

# The host-side reference: the pinned fork, the server from the same commit.
tools/ephsrv-fork.sh "$SCRATCH/swe" > "$SCRATCH/fork.log" 2>&1 || { cat "$SCRATCH/fork.log"; fail "the pinned fork"; }
(cd "$SCRATCH/ctx" && make -s -f Makefile.ephsrv SWE_HOME="$SCRATCH/swe" -j4 > "$SCRATCH/make.log" 2>&1) ||
  { tail -20 "$SCRATCH/make.log"; fail "the host build"; }

CID=$(docker run -d -p "127.0.0.1:$PORT:47190" -v "$ROOT/ephem:/ephe:ro" "$TAG")
for _ in $(seq 1 100); do
  curl -fsS "http://127.0.0.1:$PORT/healthz" > /dev/null 2>&1 && break
  sleep 0.1
done
[ "$(curl -fsS "http://127.0.0.1:$PORT/healthz")" = ok ] || fail "/healthz"
[ "$(docker exec "$CID" id -u)" != 0 ] || fail "the server runs as root"
echo "  health  /healthz ok; running as uid $(docker exec "$CID" id -u)"

"$SCRATCH/ctx/astrolog-ephd" --port "$((PORT + 1))" --ephe "$ROOT/ephem" --threads 1 > "$SCRATCH/host.log" 2>&1 &
HOST_PID=$!
for _ in $(seq 1 50); do grep -q "evt=listen port=" "$SCRATCH/host.log" && break; sleep 0.1; done
CLI="$SCRATCH/ctx/eph_wsclient"
ARGS=(--objs 0,1,2,3,4,5,6,7,8,9,15,10004 --jd 2451545.0 --step 86400 --count 400 --quiet)
"$CLI" --port "$PORT" "${ARGS[@]}" --out "$SCRATCH/container.txt" || fail "the container's window"
"$CLI" --port "$((PORT + 1))" "${ARGS[@]}" --out "$SCRATCH/host.txt" || fail "the host's window"
python3 - "$SCRATCH/container.txt" "$SCRATCH/host.txt" > "$SCRATCH/cmp.txt" << 'PY' || { cat "$SCRATCH/cmp.txt"; fail "the container's window is not the host's"; }
import sys
c = open(sys.argv[1]).read().splitlines()
h = open(sys.argv[2]).read().splitlines()
if len(c) != len(h) or not h:
    print("row counts differ: %d vs %d" % (len(c), len(h))); sys.exit(1)
worst = [0.0] * 6
moved = 0
for a, b in zip(c, h):
    fa, fb = a.split(), b.split()
    if fa[:3] != fb[:3] or len(fa) != len(fb):
        print("names or flags differ:\n  %s\n  %s" % (a, b)); sys.exit(1)
    if a != b: moved += 1
    for i in range(6):
        worst[i] = max(worst[i], abs(float.fromhex(fa[3 + i]) - float.fromhex(fb[3 + i])))
tol = [1e-10, 1e-10, 1e-10, 1e-8, 1e-8, 1e-8]
print("%d rows, %d not bit-identical, worst position %.2g deg, speed %.2g deg/day"
      % (len(h), moved, max(worst[:3]), max(worst[3:])))
sys.exit(0 if all(w <= t for w, t in zip(worst, tol)) else 1)
PY
echo "  answers $(cat "$SCRATCH/cmp.txt")"
lim=$(docker logs "$CID" 2>&1 | sed -n 's/^open-file limit \([0-9]*\) (soft).*/\1/p')
[ "${lim:-0}" -gt 1024 ] || fail "the open-file limit in the container is ${lim:-unknown}"
echo "  limits  open-file limit raised to $lim"

for _ in $(seq 1 60); do
  [ "$(docker inspect -f '{{.State.Health.Status}}' "$CID")" = healthy ] && break
  sleep 1
done
# The first probe runs at --interval, 30 s; a start period does not hurry it.
[ "$(docker inspect -f '{{.State.Health.Status}}' "$CID")" = healthy ] || fail "Docker's HEALTHCHECK never reported healthy"
echo "  health  Docker's HEALTHCHECK reports healthy"

docker stop -t 15 "$CID" > /dev/null
code=$(docker inspect -f '{{.State.ExitCode}}' "$CID")
docker logs "$CID" 2>&1 | grep -q "drained; exiting" || fail "docker stop did not drain"
[ "$code" = 0 ] || fail "docker stop: exit code $code"
echo "  stop    docker stop drains and exits 0"
echo "IMAGE PASS: $TAG"
