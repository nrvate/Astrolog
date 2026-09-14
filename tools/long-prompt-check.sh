#!/bin/sh
# A command line typed at the console prompt that is longer than the prompt
# reads is refused whole -- never split, with its tail run as a second command.
#
#   tools/long-prompt-check.sh ./astrolog
#
# InputString() read one fgets() into the caller's buffer and never asked
# whether the line had ended, so the rest of a long line stayed in stdin and
# was read as the NEXT input. The post-build review widened the command-line
# prompt's buffer from 255 to 1020 bytes, which moved the split rather than
# removing it: a 1,030-character line ending in "-Hc" drew its chart and then
# ran "...LLL -Hc" at the next prompt, printing the credits nobody asked for.
# Every NInputRange() prompt had the same hole with a 128-byte buffer.
#
# The line is fed on stdin, one over-long command line and then ".", and the
# check is that the tail's "-Hc" never runs and the prompt says why.
bin=${1:?usage: long-prompt-check.sh <astrolog binary>}
[ -x "$bin" ] || { echo "no such binary: $bin"; exit 2; }
long=$(python3 -c "print('L' * 1030)")
out=$(printf '%s\n.\n' "-n -YXt $long -Hc" | \
  timeout 30 "$bin" -i astrolog.as _X -Q -n 2>&1)
rc=$?
[ $rc -lt 128 ] || { echo "FAIL: the binary died on signal $((rc - 128))"; exit 1; }
case $out in
  *"Input command line"*) ;;
  *) echo "FAIL: no prompt in the output -- this checked nothing"; exit 1 ;;
esac
case $out in
  *"Walter D. Pullen"*)
    echo "FAIL: the over-long line was split and its tail (-Hc) ran"
    exit 1 ;;
esac
case $out in
  *"too long"*) ;;
  *) echo "FAIL: the over-long line was not refused with a message"; exit 1 ;;
esac
echo "ok: an over-long prompt line is refused whole, and says so"
