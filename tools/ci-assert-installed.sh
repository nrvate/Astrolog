#!/bin/sh
# Assert an installed Astrolog finds its data from an unrelated directory.
#
#   tools/ci-assert-installed.sh ~/.local/bin/astrolog
#
# QT_CI_PLAN.md item 4.5, applied to "make install" rather than to a
# package. The install copies the binaries to $PREFIX/bin and their data
# to $PREFIX/share/astrolog (QT_GUI_PLAN.md item 292), so the thing that
# can break is data resolution, not linking: GetDataDir() (io.cpp) has to
# find ../share/astrolog from the binary, and a bare "astrolog" typed at a
# prompt has to be found on PATH before it can. That is why this runs
# from "/", and runs the binary both ways.
#
# A REAL BINARY, NOT A WRAPPER. Until 2026-10-25 "make install" wrote a
# two-line script that ran the in-tree binary by absolute path, and every
# check below passes for one of those as long as the checkout is still
# there -- which, on the machine that ran the install, it always is. So
# the shape is checked first: an install that still points into a
# checkout is the old install, whatever it computes.
#
# ASSERT A BODY THAT CANNOT COMPUTE WITHOUT THE FILES. Astrolog falls back
# to the Moshier formulas silently: with ephem/ deleted entirely the Sun
# still reads 24Gem07'46", and only its velocity moves, in the seventh
# decimal. A swetest-style "^Sun +24Gem07" check therefore passes on an
# install with no ephemeris in it at all -- that was the assertion this
# project specified for two drafts before anyone removed the files and
# looked. Chiron reads 0Ari00'00" when the ephemeris is missing, which is
# binary and unmistakable. And Chiron is asked of Ephemeris Prometheia
# alone too ("-bE prometheia"), because the default chain falls back to
# the Swiss files: an install missing Prometheia's DE441 cut or its body
# catalog would still pass on the default chain alone.
#
# Exit 0 when the binary is a real executable, runs from an unrelated cwd
# by path and by name, and Chiron is not 0Ari00 either way or from either
# source.
set -eu

bin=${1:?usage: ci-assert-installed.sh <path to installed binary>}
case $bin in /*) ;; *) bin=$(cd "$(dirname "$bin")" && pwd)/$(basename "$bin");; esac

if [ ! -x "$bin" ]; then
  echo "NOT INSTALLED: $bin is not an executable file."
  exit 1
fi
if [ "$(head -c 2 "$bin")" = "#!" ]; then
  echo "NOT INSTALLED: $bin is a script, not the program:"
  sed -n 's/^exec /  exec /p' "$bin" | head -1
  echo "== that is the wrapper the old \"make install\" wrote, which runs a"
  echo "== checkout's binary and stops working when the checkout moves."
  exit 1
fi

# -YQ 0 and </dev/null, both: with no astrolog.as found -- the very case
# this must refuse -- the compiled default pages a long chart at 24 rows
# and waits on stdin for Return. Where stdin is a socket that never closes
# (a session's shell tool) that waited an hour; where it is closed (a CI
# runner) the program quit after one page, and only luck put Chiron on it.
chart="-qa 6 15 1990 12:00 0 122W19 47N36 -R1 _X -YQ 0"
dir=$(dirname "$bin"); name=$(basename "$bin")
for how in path name; do
  for src in "" "-bE prometheia"; do
    # shellcheck disable=SC2086
    if [ "$how" = path ]; then
      out=$(cd / && env -u ASTROLOG "$bin" $chart $src </dev/null 2>&1) || true
    else
      out=$(cd / && env -u ASTROLOG PATH="$dir:/usr/bin:/bin" "$name" $chart $src </dev/null 2>&1) || true
    fi
    what="run by $how from /${src:+, $src}"
    if [ -z "$out" ]; then
      echo "NOT INSTALLED: no output at all, $what."
      exit 1
    fi
    chiron=$(printf '%s\n' "$out" | grep -E '^Chir' | head -1 || true)
    if [ -z "$chiron" ]; then
      echo "NOT INSTALLED: no Chiron line in the chart, $what. Output began:"
      printf '%s\n' "$out" | head -5 | sed 's/^/  /'
      exit 1
    fi
    case $chiron in
      *0Ari00*)
        echo "EPHEMERIS NOT FOUND, $what: $chiron"
        echo "0Ari00'00\" is Astrolog's no-ephemeris answer. The binary ran but"
        echo "could not resolve its data from $dir/../share/astrolog."
        printf '%s\n' "$out" | grep -iE "not found|could not answer|no ephemeris" | head -2 | sed 's/^/  /'
        exit 1
        ;;
    esac
  done
done

echo "installed ok: a real binary, run from / by path and by name, $chiron"
