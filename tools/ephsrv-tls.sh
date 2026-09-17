#!/bin/bash
# tools/ephsrv-tls.sh -- the ephemeris server's TLS gate.
#
# The golden gate run with TLS=1 proves wss:// answers carry the same bits
# as ws:// ones. This proves the rest of what serving TLS means
# (EPHEMERIS_SERVER_PRODUCTION_PLAN.md Phase 1), with a throwaway CA and
# certificates made in the scratch dir -- nothing here touches a real one:
#
#   start   a key that does not match its certificate, and an expired
#           certificate, each refuse to start the server, naming the file;
#           a certificate without a key is a usage error
#   client  a verifying client refuses a wrong name and an untrusted CA
#           (exit 3, the handshake), and a plain client gets no WebSocket
#           from a TLS listener (exit 2)
#   floor   TLS 1.1 is refused; TLS 1.2 and 1.3 are accepted
#   reload  SIGHUP with a broken pair keeps the old certificate; SIGHUP with
#           a new pair from a second CA leaves a connection that was
#           streaming across it finishing cleanly, and the next connection
#           verifies only against the new CA
#
# Knobs (env): SWE_HOME, EPH, PORT (default 28900 + pid % 90, and PORT + 1, below the
# ephemeral range; ephsrv-robust.sh says why).
#
# Exit 0 with "TLS PASS"; nonzero with the first failure named.

set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
EPH=${EPH:-$ROOT/ephem}
PORT=${PORT:-$((28900 + $$ % 90))}
SCRATCH=$(mktemp -d /tmp/ephsrv-tls.XXXXXX)
EPHD_PID=
CLI="$ROOT/eph_wsclient"

cleanup() {
  [ -n "$EPHD_PID" ] && kill "$EPHD_PID" 2>/dev/null
  rm -rf "$SCRATCH"
}
trap cleanup EXIT

fail() { echo "TLS FAIL: $*"; [ -f "$SCRATCH/ephd.log" ] && sed 's/^/  ephd: /' "$SCRATCH/ephd.log" | tail -8; exit 1; }

[ -x "$ROOT/astrolog-ephd" ] && [ -x "$CLI" ] || make -s ephsrv >/dev/null
command -v openssl >/dev/null || fail "no openssl on PATH"

cd "$SCRATCH"
ssl() { openssl "$@" >> openssl.log 2>&1 || fail "openssl $1 failed (openssl.log)"; }
# A CA, and a localhost certificate it signs: mkcert CA NAME DAYS
mkca() {
  ssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes \
    -keyout "$1.key" -out "$1.pem" -days 2 -subj "/CN=$1"
}
mkcert() {
  ssl req -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes \
    -keyout "$2.key" -out "$2.csr" -subj /CN=localhost
  printf 'subjectAltName=DNS:localhost,IP:127.0.0.1\n' > ext.cnf
  ssl x509 -req -in "$2.csr" -CA "$1.pem" -CAkey "$1.key" -CAcreateserial \
    -out "$2.pem" -days "$3" -extfile ext.cnf
}
mkca ca1; mkca ca2
mkcert ca1 srv1 1
mkcert ca2 srv2 1
mkcert ca1 expired 0          # notAfter is the signing second
cd "$ROOT"
S="$SCRATCH"

start() {   # start ARGS... ; waits for the listening line
  "$ROOT/astrolog-ephd" --port "$PORT" --ephe "$EPH" --threads 2 "$@" \
    > "$S/ephd.log" 2>&1 &
  EPHD_PID=$!
  for _ in $(seq 1 50); do
    grep -q "evt=listen port=" "$S/ephd.log" 2>/dev/null && return 0
    kill -0 "$EPHD_PID" 2>/dev/null || return 1
    sleep 0.1
  done
  return 1
}
refuses_start() {   # refuses_start WHAT GREP ARGS...
  local what=$1 want=$2; shift 2
  if "$ROOT/astrolog-ephd" --port "$PORT" --ephe "$EPH" --threads 1 "$@" \
       > "$S/refuse.log" 2>&1; then
    fail "$what: the server started"
  fi
  grep -q -- "$want" "$S/refuse.log" || { cat "$S/refuse.log"; fail "$what: no \"$want\" in its message"; }
  echo "  start   $what refused"
}
ask() {   # ask EXPECTED-EXIT LABEL CLIENT-ARGS...
  local want=$1 label=$2; shift 2
  set +e
  "$CLI" --port "$PORT" --objs 10,301 --count 2 --quiet "$@" > "$S/cli.out" 2> "$S/cli.err"
  local got=$?
  set -e
  [ "$got" = "$want" ] || { cat "$S/cli.err"; fail "$label: exit $got, wanted $want"; }
}

# -- start -----------------------------------------------------------------
sleep 1   # the expired certificate's notAfter must be in the past
refuses_start "a key that does not match" "does not match" \
  --tls-cert "$S/srv1.pem" --tls-key "$S/srv2.key"
refuses_start "an expired certificate" "expired on" \
  --tls-cert "$S/expired.pem" --tls-key "$S/expired.key"
refuses_start "a certificate without a key" "usage:" \
  --tls-cert "$S/srv1.pem"

# -- client ----------------------------------------------------------------
start --tls-cert "$S/srv1.pem" --tls-key "$S/srv1.key" || fail "server did not start"
grep -q "evt=listen port=$PORT scheme=wss " "$S/ephd.log" || fail "the listen line does not say wss"
ask 0 "trusted" --tls --ca "$S/ca1.pem"
echo "  client  a verifying client is answered"
ask 3 "wrong name" --tls --ca "$S/ca1.pem" --sni wrong.example
grep -q "hostname mismatch" "$S/cli.err" || fail "wrong name: refused for another reason: $(cat "$S/cli.err")"
ask 3 "untrusted CA" --tls --ca "$S/ca2.pem"
grep -q "certificate verify failed" "$S/cli.err" || fail "untrusted: refused for another reason: $(cat "$S/cli.err")"
ask 2 "plain client"
echo "  client  wrong name, untrusted CA and plain text refused"

# -- floor -----------------------------------------------------------------
# proto FLAG [PORT] -> 0 when a handshake at that version completes. The
# client runs at security level 0: this OpenSSL refuses TLS 1.1 on the
# client side by default, so without it "1.1 refused" was the client
# talking, never the server -- measured against an s_server that allows
# 1.1, which the plain client could not reach and this one does.
proto() {
  echo | timeout 5 openssl s_client -connect "127.0.0.1:${2:-$PORT}" "$1" \
    -cipher 'DEFAULT@SECLEVEL=0' -CAfile "$S/ca1.pem" > "$S/sclient.out" 2>&1
  # "New, <version>, Cipher is <suite>" is printed for every completed
  # handshake and names (NONE) for a failed one. Not the session block's
  # "Protocol :" line: TLS 1.3 prints no session block here, so a probe on
  # that line called every 1.3 handshake refused.
  grep -q "^New, .*Cipher is " "$S/sclient.out" &&
    ! grep -q "Cipher is (NONE)" "$S/sclient.out"
}
# The probe must be able to see an accepted 1.1, or its refusal means
# nothing: a local s_server that allows it.
openssl s_server -accept "$((PORT + 1))" -cert "$S/srv1.pem" -key "$S/srv1.key" \
  -tls1_1 -cipher 'DEFAULT@SECLEVEL=0' -www > "$S/sserver.log" 2>&1 &
SSERVER_PID=$!
sleep 0.5
proto -tls1_1 "$((PORT + 1))" || { kill $SSERVER_PID 2>/dev/null; fail "the TLS 1.1 probe cannot see an accepted 1.1 handshake"; }
kill $SSERVER_PID 2>/dev/null; wait $SSERVER_PID 2>/dev/null || true
if proto -tls1_1; then fail "TLS 1.1 was accepted"; fi
proto -tls1_2 || fail "TLS 1.2 was refused"
proto -tls1_3 || fail "TLS 1.3 was refused"
echo "  floor   TLS 1.1 refused, 1.2 and 1.3 accepted"

# -- reload ----------------------------------------------------------------
# A broken pair first: the files change, SIGHUP, and the old certificate
# must still be the one served.
cp "$S/srv1.pem" "$S/live.pem"; cp "$S/srv1.key" "$S/live.key"
kill "$EPHD_PID"; wait "$EPHD_PID" 2>/dev/null || true; EPHD_PID=
start --tls-cert "$S/live.pem" --tls-key "$S/live.key" || fail "server did not restart"
cp "$S/srv2.key" "$S/live.key"            # certificate 1, key 2: mismatched
kill -HUP "$EPHD_PID"
for _ in $(seq 1 30); do grep -q "sig=SIGHUP" "$S/ephd.log" && break; sleep 0.1; done
grep -q "keeping the current certificate" "$S/ephd.log" || fail "a broken pair was not refused on SIGHUP"
ask 0 "after a refused reload" --tls --ca "$S/ca1.pem"
echo "  reload  a broken pair is refused and the old certificate kept"

# Now a good pair from the second CA, while a connection streams across the
# signal: 30 repeats, each reading nothing for 100 ms after sending.
cp "$S/srv1.key" "$S/live.key"
"$CLI" --port "$PORT" --tls --ca "$S/ca1.pem" --objs 10,301,199,299,4 --count 400 \
  --step 3600 --repeat 30 --sleep-ms 100 --quiet > "$S/long.out" 2> "$S/long.err" &
LONG_PID=$!
sleep 0.8
kill -0 "$LONG_PID" 2>/dev/null || fail "the long connection ended before the reload; lengthen it"
cp "$S/srv2.pem" "$S/live.pem"; cp "$S/srv2.key" "$S/live.key"
kill -HUP "$EPHD_PID"
for _ in $(seq 1 30); do grep -q "evt=tls.reload" "$S/ephd.log" && break; sleep 0.1; done
grep -q "evt=tls.reload cert=$S/live.pem " "$S/ephd.log" || fail "the good pair was not reloaded"
sleep 0.3
kill -0 "$LONG_PID" 2>/dev/null || fail "the long connection ended before the reload took effect; lengthen it"
wait "$LONG_PID" || { cat "$S/long.err"; fail "the connection streaming across the reload failed"; }
ask 0 "new connection, new CA" --tls --ca "$S/ca2.pem"
ask 3 "new connection, old CA" --tls --ca "$S/ca1.pem"
echo "  reload  a live connection survives; new connections get the new certificate"

echo "TLS PASS"
