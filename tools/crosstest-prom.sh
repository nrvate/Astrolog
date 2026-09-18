#!/bin/sh
# tools/crosstest-prom.sh -- our wire client against Ephemeris Prometheia's
# server, checking the PER-OBJECT ERROR CONTRACT both projects now agree on.
#
#   tools/crosstest-prom.sh [prometheiad] [ephemeris-file]
#   tools/crosstest-prom.sh --selftest        # prove this script can fail
#
# This is leg 2 of the cross-test runbook (the other project's CROSS-TEST.md),
# from our side. It is run BY HAND like every other eph gate; nothing in
# "make check" starts a foreign daemon.
#
# WHY THIS LEG IS FIRST. It is the cheapest -- one object per case, no
# numerical comparison at all -- and a disagreement about what an error MEANS
# would otherwise surface much later, misattributed, inside a numeric leg. It
# has already earned that place: the first run of these probes, by hand, found
# "Beta Sco" answered by the server and refused as ambiguous by our own
# Prometheia plugin. One name, one catalogue, two answers. That is now fixed
# in the plugin and in their SERVER.md, and this script is what stops it
# coming back.
#
# The expectations below are THEIR contract, from SERVER.md's "Per-object
# errors" table, transcribed here as codes rather than prose. A.17: 1 unknown,
# 2 unsupported, 3 coverage, 4 data, 5 undefined point, 6 ambiguous,
# 7 numerical, 8 internal.
#
# The daemon is started here on a loopback port of its own and stopped by the
# PID this script started, never by name -- other servers belonging to other
# people run on this machine.

set -u

PROMD="${1:-/shares/ephemeris-prometheia/build/prometheiad}"
EPHE="${2:-/shares/ephemeris-prometheia/ephe/linux_p1550p2650.440}"
CLIENT="./eph_wsclient"
PORT="${CROSSTEST_PORT:-47291}"
JD=2451545.0
WORK="${TMPDIR:-/nvm/work}/crosstest-prom.$$"
SELFTEST=0

[ "${1:-}" = "--selftest" ] && { SELFTEST=1; PROMD=/shares/ephemeris-prometheia/build/prometheiad; }

for f in "$PROMD" "$EPHE" "$CLIENT"; do
  if [ ! -e "$f" ]; then
    echo "crosstest-prom: $f is not there; skipping (this gate needs the"
    echo "  Prometheia build and our own \"make ephsrv\")"
    exit 0
  fi
done

mkdir -p "$WORK" || exit 1
PID=""
cleanup() {
  # By the PID we started, never by name.
  [ -n "$PID" ] && kill "$PID" 2>/dev/null
  [ -n "$PID" ] && wait "$PID" 2>/dev/null
  rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

"$PROMD" --ephemeris "$EPHE" --bind 127.0.0.1 --port "$PORT" --threads 1 \
  > "$WORK/daemon.log" 2>&1 &
PID=$!

# Wait until the daemon ANSWERS, not until it says it is listening. Those
# are different moments, and the difference is not cosmetic: the first run
# of this script reported FIVE false mismatches -- including the very case
# the two projects had just agreed on -- because the probes began as soon
# as the log line appeared. A readiness check that is not the thing being
# waited for is how a harness invents findings, and a table of error codes
# is the worst place for that, because wrong answers there look exactly
# like real disagreements.
i=0
while [ $i -lt 100 ]; do
  if "$CLIENT" --host 127.0.0.1 --port "$PORT" --quiet --jd "$JD" --meta \
      --objs 10 2>/dev/null | grep -q "err=0"; then
    break
  fi
  kill -0 "$PID" 2>/dev/null || { echo "crosstest-prom: prometheiad exited:"
    cat "$WORK/daemon.log"; exit 1; }
  i=$((i+1)); sleep 0.2
done
if [ $i -ge 100 ]; then
  echo "crosstest-prom: prometheiad never answered a trial query:"
  cat "$WORK/daemon.log"
  exit 1
fi

cFail=0
probe() {   # probe <name> <expected-code> <client args...>
  name="$1"; want="$2"; shift 2
  out=$("$CLIENT" --host 127.0.0.1 --port "$PORT" --quiet --jd "$JD" --meta \
    "$@" 2>&1)
  if printf '%s' "$out" | grep -q "server ERROR"; then
    got=$(printf '%s' "$out" | sed -n 's/.*server ERROR \([0-9]*\).*/E\1/p' \
      | head -1)
  else
    got=$(printf '%s' "$out" | sed -n 's/.*err=\([0-9]*\).*/\1/p' | head -1)
  fi
  [ -z "$got" ] && got="(none)"
  [ -n "${CROSSTEST_DEBUG:-}" ] && { echo "--- $name"; printf '%s\n' "$out"; }
  if [ "$got" = "$want" ]; then
    printf '  %-34s %-4s ok\n' "$name" "$got"
  else
    printf '  %-34s %-4s MISMATCH, contract says %s\n' "$name" "$got" "$want"
    cFail=$((cFail+1))
  fi
}

echo "Leg 2: the per-object error contract, our client against prometheiad."
echo "Expectations are their SERVER.md table, as codes."

# Answered, so that a run where EVERYTHING errors cannot read as a pass --
# the failure mode a table of error expectations invites.
probe "a body that is served (NAIF 4)"      0 --objs 4
probe "the Sun (NAIF 10)"                   0 --objs 10
# A Bayer designation with no component number: the brightest, NOT ambiguous.
# This is the case the two projects disagreed about.
probe "Bayer, no component (Beta Sco)"      0 --stars "Beta Sco"
probe "a Flamsteed pair (61 Cyg)"           0 --stars "61 Cyg"

probe "an unknown star"                     1 --stars "No Such Star"
probe "an unknown designation"              1 --desig "NoSuchThing"
probe "a planet-centre id (499)"            1 --objs 499
probe "a negative NAIF id"                  2 --objs -5
probe "the solar-system barycentre"         2 --objs 0
probe "an instant outside coverage"         3 --objs 4 --jd2 -900000.0
probe "helio with correction mask 7"        E11 --profile obs=helio,corr=7 --objs 4

if [ "$SELFTEST" = 1 ]; then
  # The gate must be able to fail. A served body cannot be code 1.
  echo "  -- selftest: the next line must MISMATCH --"
  probe "SELFTEST (a served body as 1)"     1 --objs 4
  if [ "$cFail" -eq 1 ]; then
    echo "crosstest-prom: selftest ok (the one planted mismatch was caught)"
    exit 0
  fi
  echo "crosstest-prom: SELFTEST FAILED -- $cFail mismatches, expected 1"
  exit 1
fi

if [ "$cFail" -gt 0 ]; then
  echo "crosstest-prom: $cFail mismatch(es) against the agreed contract."
  echo "  Each is a bug in one project or the other, and the table says"
  echo "  which side intends what."
  exit 1
fi
echo "crosstest-prom: the error contract agrees in every case."
exit 0
