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
#   - answer a window byte-identical to a server built on this machine
#     against the same pinned fork (tools/ephsrv-fork.sh), over the wire
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
for _ in $(seq 1 50); do grep -q "listening on port" "$SCRATCH/host.log" && break; sleep 0.1; done
CLI="$SCRATCH/ctx/eph_wsclient"
ARGS=(--objs 0,1,2,3,4,5,6,7,8,9,15,10004 --jd 2451545.0 --step 86400 --count 400 --quiet)
"$CLI" --port "$PORT" "${ARGS[@]}" --out "$SCRATCH/container.txt" || fail "the container's window"
"$CLI" --port "$((PORT + 1))" "${ARGS[@]}" --out "$SCRATCH/host.txt" || fail "the host's window"
cmp -s "$SCRATCH/container.txt" "$SCRATCH/host.txt" || fail "the container's window differs from the host's"
echo "  answers $(wc -l < "$SCRATCH/host.txt") rows byte-identical to a host build of the same pin"

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
