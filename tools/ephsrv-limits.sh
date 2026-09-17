#!/bin/bash
# tools/ephsrv-limits.sh -- the ephemeris server's protocol-3 and limits gate.
#
# EPHEMERIS_SERVER_PRODUCTION_PLAN.md Phases 3 and 4, each asserted:
#
#   versions  a version-4 client is welcomed in 4, and so is a NEWER one
#             (protoMax 5, the session the lower of the two); a version-3
#             client gets ERROR 8 it can read, in its own envelope version
#             and layout, telling it to update; a client that needs a
#             version newer than this server's gets ERROR 8 in 4
#   hello     a REQUEST before HELLO is refused ERROR 1; a connection that
#             says nothing is closed at --hello-seconds and counted
#   caps      a third connection from one address is refused under
#             --max-conns-per-ip 2, and a second one under --max-conns 1;
#             both counted; closing one frees its place
#   budget    under a 5000-cell budget: 5000 cells served, the next request
#             at once refused ERROR 6 with when to ask again, served after
#             the refill; a known token has a budget of its own
#   tokens    --require-token refuses no token and an unknown one (ERROR 7),
#             welcomes a listed one, and never writes a token to the log
#
# The golden gate's PROTO=5 run is the other half: a newer client welcomed
# in 4 and answered bit-exact.
#
# Knobs (env): EPH, PORT (default 27800 + pid % 190, and PORT + 1..4: the
# one range no other gate or suite group uses -- the first choice, 28600,
# sat inside ephsrv-cache.sh's 28400-28699 and a run lost its port to a
# server already listening there).
#
# Exit 0 with "LIMITS PASS"; nonzero with the first failure named.

set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
EPH=${EPH:-$ROOT/ephem}
PORT=${PORT:-$((27800 + $$ % 190))}
SCRATCH=$(mktemp -d /tmp/ephsrv-limits.XXXXXX)
PIDS=()
CLI="$ROOT/eph_wsclient"
S="$SCRATCH"

cleanup() {
  for p in "${PIDS[@]:-}"; do [ -n "$p" ] && { kill "$p" 2>/dev/null || true; }; done
  rm -rf "$SCRATCH"
  true
}
trap cleanup EXIT

fail() { echo "LIMITS FAIL: $*"; for f in "$S"/*.log; do [ -f "$f" ] && sed "s|^|  $(basename "$f"): |" "$f" | tail -5; done; exit 1; }

[ -x "$ROOT/astrolog-ephd" ] && [ -x "$CLI" ] || make -s ephsrv >/dev/null

SRV_PID=
start() {   # start PORT LOG ARGS...
  local port=$1 log=$2; shift 2
  "$ROOT/astrolog-ephd" --port "$port" --threads 2 --ephe "$EPH" "$@" > "$log" 2>&1 &
  SRV_PID=$!
  PIDS+=("$SRV_PID")
  for _ in $(seq 1 50); do
    grep -q "evt=listen port=" "$log" 2>/dev/null && return 0
    kill -0 "$SRV_PID" 2>/dev/null || return 1
    sleep 0.1
  done
  return 1
}
stop() { kill "$SRV_PID" 2>/dev/null; wait "$SRV_PID" 2>/dev/null || true; }
metric() {
  curl -s "http://127.0.0.1:$1/metrics" |
    awk -v n="$2" '$1 == n { print $2; f = 1 } END { if (!f) print 0 }'
}
ask() {   # ask EXPECTED-EXIT LABEL PORT CLIENT-ARGS...
  local want=$1 label=$2 port=$3; shift 3
  set +e
  "$CLI" --port "$port" --objs 10,301 --count 2 "$@" > "$S/cli.out" 2> "$S/cli.err"
  local got=$?
  set -e
  [ "$got" = "$want" ] || fail "$label: exit $got, wanted $want: $(cat "$S/cli.err")"
}

# -- versions --------------------------------------------------------------
start "$PORT" "$S/a.log" --cells-per-sec 0 || fail "server did not start"
ask 0 "a version-4 client" "$PORT"
grep -q "^WELCOME v4 " "$S/cli.out" || fail "a version-4 client was not welcomed in 4: $(cat "$S/cli.out")"
ask 0 "a newer client" "$PORT" --proto 5
grep -q "^WELCOME v4 " "$S/cli.out" || fail "a protocol-5 client was not welcomed in 4: $(cat "$S/cli.out")"
ask 2 "a version-3 client" "$PORT" --proto 3
grep -q "server ERROR 8 (HELLO, v3):.*update" "$S/cli.err" ||
  fail "a version-3 client did not get a readable ERROR 8 in its own layout: $(cat "$S/cli.err")"
ask 2 "a client that needs protocol 5" "$PORT" --proto 6 --proto-min 5
grep -q "server ERROR 8 (HELLO, v4) closing" "$S/cli.err" ||
  fail "a client needing protocol 5 was not refused in 4: $(cat "$S/cli.err")"
echo "  versions 4 and newer welcomed in 4; 3 told to update, in 3; a client needing 5 refused"

# -- hello -----------------------------------------------------------------
ask 2 "a REQUEST before HELLO" "$PORT" --no-hello --quiet
grep -q "server ERROR 1 .*closing.*HELLO first" "$S/cli.err" ||
  fail "a REQUEST before HELLO: $(cat "$S/cli.err")"
stop
start "$PORT" "$S/b.log" --cells-per-sec 0 --hello-seconds 1 || fail "server did not start"
ask 2 "a silent connection" "$PORT" --no-hello --idle-ms 2500 --quiet
[ "$(metric "$PORT" ephd_hello_timeouts_total)" = 1 ] || fail "the silent connection was not counted as a HELLO timeout"
echo "  hello    a REQUEST before HELLO refused; a silent connection closed at the deadline"
stop

# -- caps ------------------------------------------------------------------
P1=$((PORT + 1))
start "$P1" "$S/c.log" --cells-per-sec 0 --max-conns-per-ip 2 || fail "server did not start"
"$CLI" --port "$P1" --objs 10 --count 1 --quiet --idle-ms 2000 > /dev/null 2>&1 & H1=$!
"$CLI" --port "$P1" --objs 10 --count 1 --quiet --idle-ms 2000 > /dev/null 2>&1 & H2=$!
PIDS+=("$H1" "$H2")
sleep 0.5
ask 2 "a third connection from one address" "$P1" --quiet
[ "$(metric "$P1" ephd_refused_connections_total)" = 1 ] || fail "the refused connection was not counted"
wait "$H1" "$H2" || fail "the two admitted connections failed"
ask 0 "a connection after the others closed" "$P1" --quiet
stop
P2=$((PORT + 2))
start "$P2" "$S/d.log" --cells-per-sec 0 --max-conns 1 || fail "server did not start"
"$CLI" --port "$P2" --objs 10 --count 1 --quiet --idle-ms 2000 > /dev/null 2>&1 & H1=$!
PIDS+=("$H1")
sleep 0.5
ask 2 "a second connection under --max-conns 1" "$P2" --quiet
wait "$H1" || fail "the admitted connection failed"
echo "  caps     per address and in total, counted, and freed on close"
stop

# -- budget and tokens -----------------------------------------------------
P3=$((PORT + 3))
printf '# accepted tokens\nlimits-gate-token-1\n' > "$S/tokens.txt"
start "$P3" "$S/e.log" --max-cells 5000 --cells-per-sec 5000 --tokens "$S/tokens.txt" ||
  fail "server did not start"
ask 0 "5000 cells within the budget" "$P3" --count 2500 --quiet
ask 2 "5000 more at once" "$P3" --count 2500 --jd 2451600.5 --quiet
grep -q "server ERROR 6 .*ask again in [0-9.]* s" "$S/cli.err" || fail "over budget: $(cat "$S/cli.err")"
ask 0 "a known token's own budget" "$P3" --count 2500 --jd 2451700.5 --quiet --token limits-gate-token-1
sleep 1.2
ask 0 "5000 cells after the refill" "$P3" --count 2500 --jd 2451800.5 --quiet
echo "  budget   refused over it with when to ask; refilled; a token budgets apart"
stop

P4=$((PORT + 4))
start "$P4" "$S/f.log" --cells-per-sec 0 --tokens "$S/tokens.txt" --require-token || fail "server did not start"
ask 2 "no token" "$P4" --quiet
grep -q "server ERROR 7 " "$S/cli.err" || fail "no token: $(cat "$S/cli.err")"
ask 2 "an unknown token" "$P4" --quiet --token not-a-token
grep -q "server ERROR 7 .*unknown token" "$S/cli.err" || fail "unknown token: $(cat "$S/cli.err")"
ask 0 "a listed token" "$P4" --quiet --token limits-gate-token-1
grep -q "limits-gate-token-1\|not-a-token" "$S/f.log" "$S/e.log" && fail "a token was written to the log"
echo "  tokens   required: none and unknown refused, listed welcomed, none logged"
stop

echo "LIMITS PASS"
