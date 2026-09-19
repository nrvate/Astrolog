#!/bin/sh
# tools/ephsrv-serve.sh -- (re)start the shared astrolog-ephd, from THIS commit.
#
#   tools/ephsrv-serve.sh [port]        # default 47391
#
# It exists because the same mistake happened three times: build, launch, keep
# working, commit, rebuild -- and the daemon the other project is measuring is
# now older than the binary on disk. Their harness caught it every time by
# comparing the process start against the binary's mtime, which is a better
# check than anything here had. The order is the fix: stop, rebuild, launch,
# and print the sha and the times so the message quoting them cannot drift
# from what is actually running. It refuses outright on a dirty tree, because
# a sha is a lie about a tree with uncommitted changes in it.
set -e
cd "$(dirname "$0")/.."
PORT=${1:-47391}
if [ -n "$(git status --porcelain)" ]; then
  echo "ephsrv-serve: the tree is dirty -- commit or stash first, or the sha" >&2
  echo "              below would not describe what this serves." >&2
  git status --porcelain >&2
  exit 1
fi
OLD=$(ss -lptnH "sport = :$PORT" 2>/dev/null | grep -o 'pid=[0-9]*' | head -1 | cut -d= -f2 || true)
[ -n "$OLD" ] && { kill "$OLD" 2>/dev/null || true; sleep 1; echo "stopped $OLD"; }
make -s ephsrv
nohup ./astrolog-ephd --bind 127.0.0.1 --port "$PORT" --threads 2 \
  --ephe "$PWD/ephem;$PWD" > /nvm/work/ephd$PORT.log 2>&1 &
sleep 3
PID=$(ss -lptnH "sport = :$PORT" 2>/dev/null | grep -o 'pid=[0-9]*' | head -1 | cut -d= -f2)
[ -n "$PID" ] || { echo "ephsrv-serve: did not start; see /nvm/work/ephd$PORT.log" >&2; exit 1; }
echo "$PID" > /nvm/work/ephd$PORT.pid
echo "astrolog-ephd on 127.0.0.1:$PORT"
echo "  commit   $(git rev-parse --short HEAD)  (clean)"
echo "  binary   $(stat -c %y astrolog-ephd | cut -c1-19)"
echo "  started  $(date -u '+%Y-%m-%d %H:%M:%S')Z   pid $PID"
