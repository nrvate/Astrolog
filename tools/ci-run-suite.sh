#!/usr/bin/env bash
# Run the Qt suite the way CI needs it run: bounded, in its own process
# group, through a pty, writing to a log that is read AFTER the fact.
#
#   tools/ci-run-suite.sh <seconds> <log> [run-qt-tests.sh arguments...]
#
#   tools/ci-run-suite.sh 420 suite.log -Yi1 ephem
#
# Exit status is the suite's, or 124 if the watchdog had to kill it.
#
# This used to be forty lines of shell inside a workflow's macOS job,
# and every one of its arrangements had been learned from a run that
# lost its own diagnostic (the comments there are preserved below as
# the reasons). Logic in a workflow can only be falsified by pushing, so
# it moved here, where "tools/ci-run-suite.sh 5 x.log" on a laptop shows
# the kill path in five seconds -- and where Linux, macOS and any future
# runner get the SAME watchdog rather than one platform's copy of it.
#
# Why each piece is the way it is:
#
# - BOUNDED, by a watchdog in this script rather than the job timeout: a
#   step's output is not served until the step ends, so an unbounded run
#   that hits the job timeout says only that the platform is unhappy.
#   This says which group it was in when it stopped, which is how a
#   ten-minute Linux hang was localised to one modal box in one run.
#
# - ITS OWN PROCESS GROUP ("set -m"), killed by group id and never by
#   name: killing the script alone left the binary running, holding the
#   pipe open, and the step hung anyway; "pkill -f" once killed the step
#   itself and printed nothing. CLAUDE.md's rule against pkill by name
#   is not a developer-machine rule.
#
# - THROUGH A PTY, because with stdout a plain file stdio block-buffers
#   and SIGKILL discards up to 4 KB of pending output -- one run left a
#   log holding exactly one line, Qt's own message on unbuffered stderr.
#   A pty makes stdio line-buffer, so at most one partial line is lost.
#   BSD script(1) and util-linux script(1) take the command differently,
#   and both runners are covered below.
#
# - TO A FILE, read afterwards, not through a pipe: an orphan holding a
#   pipe means "tail" never sees EOF.
#
# - "|| rc=$?", not "; rc=$?": under set -e a wait that reports 143 is a
#   failing command, and the diagnostic after it never ran.
#
# - STDIN FROM /dev/null, inside the pty. A pty makes stdin a terminal,
#   and a startup check that expects EOF -- the console program waits
#   for a keypress after an error when stdin is a tty -- waits forever.
#   Measured: the first full run through this script sat in the
#   "Ephemeris search path" startup checks until the watchdog killed it
#   at 600 s, after the whole suite had passed. The old workflow step
#   had the same shape and never hung only because a runner's stdin
#   already reads EOF.
#
# - ASTROLOG_QT_TIME and ASTROLOG_QT_TEST_VERBOSE default on, so the log
#   names each group's wall time and each menu item before it fires: the
#   difference between knowing the group a hang is in and knowing the
#   item.
#
# The whole log is the artifact worth keeping; "tail -80" here is enough
# to see where a failure stopped and not enough for anything else.
set -u
limit=${1:?usage: ci-run-suite.sh <seconds> <log> [run-qt-tests.sh args...]}
log=${2:?usage: ci-run-suite.sh <seconds> <log> [run-qt-tests.sh args...]}
shift 2
cd "$(dirname "$0")/.."
[ -x ./run-qt-tests.sh ] || { echo "no ./run-qt-tests.sh here"; exit 2; }

export ASTROLOG_QT_TIME=${ASTROLOG_QT_TIME:-1}
export ASTROLOG_QT_TEST_VERBOSE=${ASTROLOG_QT_TEST_VERBOSE:-1}

# A pty if script(1) is here. util-linux takes "-c <command string>";
# BSD (macOS) takes the command and its arguments after the typescript
# file. Quote each argument for the string form so "-Yi1 ephem" and a
# path with a space both survive.
if script --version >/dev/null 2>&1; then
  cmd="./run-qt-tests.sh"
  for a in "$@"; do cmd="$cmd '$(printf '%s' "$a" | sed "s/'/'\\\\''/g")'"; done
  start() { script -qec "$cmd </dev/null" /dev/null; }
elif command -v script >/dev/null 2>&1; then
  start() { script -q /dev/null sh -c './run-qt-tests.sh "$@" </dev/null' sh "$@"; }
else
  echo "== no script(1); running without a pty, so a killed run may lose buffered output"
  start() { ./run-qt-tests.sh "$@" </dev/null; }
fi

set -m
start "$@" > "$log" 2>&1 &
suite=$!
set +m

waited=0
while kill -0 "$suite" 2>/dev/null; do
  [ "$waited" -lt "$limit" ] || break
  sleep 1; waited=$((waited + 1))
done

killed=0
if kill -0 "$suite" 2>/dev/null; then
  echo "== suite still running after ${limit}s; killing process group $suite"
  kill -TERM -- "-$suite" 2>/dev/null || kill -TERM "$suite" 2>/dev/null || true
  sleep 3
  kill -KILL -- "-$suite" 2>/dev/null || true
  killed=1
fi
rc=0; wait "$suite" || rc=$?
[ "$killed" -eq 0 ] || rc=124

echo "== last 80 lines of $log"
tail -80 "$log"
summary=$(grep -hoE '^(PASS|FAIL): .*' "$log" 2>/dev/null | tail -1)
group=$(grep -hoE '^== [A-Z].*' "$log" 2>/dev/null | tail -1)
if [ "$rc" -ne 0 ]; then
  fails=$(grep -nE '^  FAIL  ' "$log" 2>/dev/null | head -40)
  [ -z "$fails" ] || { echo "== failing assertions (first 40):"; printf '%s\n' "$fails"; }
  if [ "$killed" -eq 1 ]; then echo "== suite exited $rc: killed by the watchdog after ${limit}s"
  else echo "== suite exited $rc"; fi
  [ -n "$group" ] && echo "== the last group named in the log: $group"
  [ -n "$summary" ] && echo "== $summary"
  exit "$rc"
fi
echo "== ${summary:-the suite printed no PASS line}"
[ -n "$summary" ] || exit 1
case $summary in PASS:*) exit 0 ;; *) exit 1 ;; esac
