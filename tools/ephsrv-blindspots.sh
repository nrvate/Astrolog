#!/bin/bash
# tools/ephsrv-blindspots.sh -- which option, if it never arrived, would this
# gate still pass?
#
#   tools/ephsrv-blindspots.sh golden          # one gate
#   tools/ephsrv-blindspots.sh --selftest      # the tool's own decisions, ~1s
#
# EVERY OTHER CHECK IN THIS PROJECT ASKS WHETHER SOMETHING CAN GO RED. This
# asks the other question, and they are not the same: a leg can be runnable,
# counted, and green for a reason that has nothing to do with what it claims
# to test.
#
# The case it was built from. ephsrv-golden.sh had three deltaTSec legs and
# all three were GEOCENTRIC, where Earth rotation does not enter -- so the
# request's delta T could have stopped reaching Swiss entirely and every one
# of the ten ephsrv gates would have stayed green. Each of those legs can go
# red; none of them could see the field go missing. That hole hid a real
# defect (registry 2.11) for as long as it existed, and no amount of
# falsification would have found it, because falsification asks the first
# question.
#
# The idea and the method are Ephemeris Prometheia's (their
# tools/check/blindspots.py). This is the same question asked on this side.
# Two of their hard-won rules are built in rather than rediscovered:
#
#   - ASSERT THE DROP REMOVED SOMETHING. An option spelling that appears in
#     no invocation removes nothing, and a gate that then passes is not blind
#     to it -- it never sent it. That is a different finding and reporting it
#     as the first one is a lie about coverage. eph_wsclient prints the count
#     it removed and this refuses to grade a zero.
#   - COMPARE AGAINST A COMMITTED MAP, not a threshold. An option that STOPS
#     being covered reds, and one that BECOMES covered reds too -- the second
#     is good news and still has to be acknowledged in the map, or the map
#     rots into a floor.
#
# How it works: one baseline run of the gate under EPH_TRACE collects every
# option the gate's client invocations ACTUALLY sent -- by execution, not by
# grepping the gate's text, so an option that stops being sent vanishes from
# the list and is noticed rather than quietly shortening the sweep. Then the
# gate is re-run once per option with EPH_DROP naming it, which makes
# eph_wsclient remove that option, with whatever it consumes, from its own
# argv. The arity lives in the client's parse loop and nowhere else.
#
# A gate that PASSES with an option dropped is blind to it. That is the
# finding, and it is not automatically a defect: some options are delivery
# (--out, --quiet) or are the gate's own plumbing (--port), and being blind
# to those is correct. The map says which.
#
# WHAT THIS CANNOT SEE, said plainly, because the case that prompted it is
# exactly the case it would have MISSED. It drops a whole option and asks
# whether anything noticed. It cannot ask whether every EFFECT of an option
# is exercised. golden sends --deltat and reds when it is dropped, so this
# reports it covered -- and it was covered that way before 2026-09-20, while
# the Earth-rotation half of that same field reached nothing and hid a real
# defect. One option, two effects, one of them untested, and a whole-option
# drop sees a covered option either way.
#
# So this is a floor on coverage, not a ceiling: it finds an option NOTHING
# depends on, which is the cheap and common hole. Finding a half-tested
# option still takes someone asking what the field is supposed to do and
# checking that a leg exists for each half. The three geocentric deltaTSec
# legs would pass this tool today.
#
# Cost: one gate run per option. Golden is ~20 s and sends about twenty, so
# about seven minutes. By hand, like every other ephsrv gate.

set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
MAP=${MAP:-tools/ephsrv-blindspots.map}
SCRATCH=$(mktemp -d /tmp/ephsrv-blind.XXXXXX)
trap 'rm -rf "$SCRATCH"' EXIT

# verdict <option> <gate-exit> <removed> <expected>
# Exit 0 when the observed state matches the map, nonzero otherwise. The one
# copy: the sweep below calls it and so does --selftest.
verdict() {
  local opt=$1 rc=$2 removed=$3 expect=$4 got
  if [ "$removed" -eq 0 ]; then
    echo "  $opt: NEVER SENT -- no invocation carries it, so this run says"
    echo "    nothing about whether the gate could see it go missing."
    return 1
  fi
  [ "$rc" -eq 0 ] && got=blind || got=covered
  if [ "$got" != "$expect" ]; then
    if [ "$got" = blind ]; then
      echo "  $opt: BLIND -- the gate passed with it dropped from $removed"
      echo "    invocation(s); the map says $expect."
    else
      echo "  $opt: now COVERED -- the gate reds without it; the map says $expect."
      echo "    Good news, and the map has to say so."
    fi
    return 1
  fi
  return 0
}

# ---- the tool's own decisions, so this one does not ship as prose --------
#
# Everything below grades a GATE. This grades the tool: the three judgements
# it makes -- covered, blind, never-sent -- against crafted inputs, with no
# gate and no server. A control plus one injection per judgement.
if [ "${1:-}" = "--selftest" ]; then
  n=0; bad=0
  st() {   # st <want ok|red> <desc> <verdict-args...>
    n=$((n + 1))
    local want=$1 desc=$2; shift 2
    if ( verdict "$@" ) > /dev/null 2>&1; then
      [ "$want" = ok ] || { echo "  SELFTEST FAIL: $desc -- passed, must have failed"; bad=$((bad + 1)); return; }
    else
      [ "$want" = red ] || { echo "  SELFTEST FAIL: $desc -- failed, must have passed"; bad=$((bad + 1)); return; }
    fi
    echo "  ok: $desc"
  }
  echo "== --selftest: the tool's own judgements"
  # verdict <option> <gate-exit> <occurrences-removed> <expected-from-map>
  st ok  "control: a covered option, gate red when dropped"   --deltat 1 3 covered
  st red "an option the map calls covered that the gate no longer sees" --deltat 0 3 covered
  st ok  "an option the map calls blind, and it is"           --out 0 2 blind
  st red "an option the map calls blind that became covered"  --out 1 2 blind
  # The never-sent case: zero occurrences removed is NOT a blind spot, and
  # grading it as one is the mistake this rule exists to prevent.
  st red "an option that was never sent (0 removed) is refused, not graded" --nope 0 0 blind
  st red "...and refused even when the map calls it covered" --nope 0 0 covered
  [ "$n" -eq 6 ] || { echo "SELFTEST FAIL: $n cases ran, expected 6"; exit 1; }
  [ "$bad" = 0 ] || { echo "SELFTEST FAIL: $bad of $n cases"; exit 1; }
  echo "BLINDSPOTS SELFTEST PASS: $n cases (2 controls + 4 injections)"
  exit 0
fi

GATE=${1:-golden}
SH="tools/ephsrv-$GATE.sh"
[ -x "$SH" ] || { echo "BLINDSPOTS FAIL: no such gate: $SH"; exit 1; }
[ -f "$MAP" ] || { echo "BLINDSPOTS FAIL: no map at $MAP -- write one (see the header)"; exit 1; }

# ---- 1. what does this gate actually send? ------------------------------
echo "== baseline: $SH, collecting the options it sends"
TRACE="$SCRATCH/trace.txt"
: > "$TRACE"
EPH_TRACE="$TRACE" "$SH" > "$SCRATCH/base.log" 2>&1 ||
  { echo "BLINDSPOTS FAIL: the gate does not pass to begin with; nothing below would mean anything"
    tail -5 "$SCRATCH/base.log"; exit 1; }
mapfile -t OPTS < <(sort -u "$TRACE")
[ "${#OPTS[@]}" -gt 0 ] ||
  { echo "BLINDSPOTS FAIL: the gate sent no options at all -- EPH_TRACE reached no client"; exit 1; }
echo "   ${#OPTS[@]} distinct options"

# ---- 2. one run per option ----------------------------------------------
fail=0; nBlind=0; nCovered=0
for opt in "${OPTS[@]}"; do
  exp=$(awk -v g="$GATE" -v o="$opt" '$1 == g && $2 == o { print $3 }' "$MAP")
  [ -n "$exp" ] || { echo "  $opt: NOT IN THE MAP -- add it with a verdict and a reason"; fail=1; continue; }
  rc=0
  EPH_DROP="$opt" "$SH" > "$SCRATCH/drop.log" 2>&1 || rc=$?
  removed=$(grep -o "EPH_DROP removed [0-9]* occurrence" "$SCRATCH/drop.log" |
            awk '{s += $3} END { print s + 0 }')
  if verdict "$opt" "$rc" "$removed" "$exp"; then
    [ "$exp" = blind ] && nBlind=$((nBlind + 1)) || nCovered=$((nCovered + 1))
    printf '   %-16s %s\n' "$opt" "$exp"
  else
    fail=1
  fi
done

[ "$fail" -eq 0 ] || { echo "BLINDSPOTS FAIL: $SH"; exit 1; }
echo "BLINDSPOTS PASS: $GATE, ${#OPTS[@]} options ($nCovered covered, $nBlind blind by design)"
