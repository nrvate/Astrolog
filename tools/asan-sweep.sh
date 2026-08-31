#!/bin/sh
#
# Astrolog (Version 8.00) File: tools/asan-sweep.sh
#
# Drive the switch surface and the graphics surface under AddressSanitizer,
# with the checked tables' range guards compiled in.
#
#   tools/asan-sweep.sh              # both surfaces
#   tools/asan-sweep.sh switches     # the 529-invocation switch matrix
#   tools/asan-sweep.sh graphics     # ~250 renders
#
# Exit status is 0 only if nothing was reported.
#
# This is a pre-release check, not a pre-commit one: minutes per run, like
# tools/win-tests.sh. It earns that on its record -- the first run of each
# half found real out-of-bounds bugs in code that had been exercised
# dozens of times without a sanitizer behind it (work log items 133-134,
# seven bugs between them, one of which crashed the release build).
#
# Why two surfaces and not one. tools/switch-matrix.sh drives the whole
# switch registry but sends stdout to /dev/null, so it covers parsing and
# almost none of the drawing. The graphics half renders to a file instead,
# which is where the chart code actually runs.
#
# Four things here are not optional, each learned the expensive way:
#
#   - The plain Makefile has no object directory of its own, so building
#     with overridden CPPFLAGS leaves sanitized objects in the repo root
#     and the next ordinary build fails to *link*, naming functions nobody
#     touched. Hence "make clean" both before and after.
#   - The binary must sit at a SHORT path. A deep one truncates the Swiss
#     ephemeris path and changes lookups rather than merely warning.
#   - The output file, by contrast, wants a LONG path on purpose: an
#     80-byte buffer took the whole output path in WriteXBitmap(), and a
#     short scratch directory would have hidden it.
#   - switch-matrix.sh pipes each run's stderr through "head -2", so a
#     sanitizer report arrives decapitated: it names the invocation and
#     nothing else. This script re-runs a reported invocation on its own
#     to recover the trace.
#
set -e
cd "$(dirname "$0")/.." || exit 1
WHICH=${1:-both}

BIN=/tmp/astrolog-asan-sweep                       # short: see above
OUT=${ASAN_SWEEP_OUT:-/tmp/asan-sweep-output-directory-deliberately-long}
mkdir -p "$OUT"
rm -f "$OUT"/hit*.log            # likewise: last run's traces are not this run's
CFG=${ASAN_SWEEP_CFG:--i nrvate.as}
Q="-qb 7 4 1976 12 0 8 122:19:55W 47:36:22N"
export ASAN_OPTIONS=detect_leaks=0
hits=0
runs=0

echo "== building the console ASan binary (QTTEST brings the range guards)"
make clean >/dev/null 2>&1
make NAME=$BIN -j4 \
  CPPFLAGS="-DQTTEST -fsanitize=address -g -O0 -Wno-write-strings \
    -Wno-narrowing -Wno-comment" \
  LIBS="-fsanitize=address -lm -lX11 -ldl" >"$OUT/build.log" 2>&1 || {
  echo "build failed, see $OUT/build.log"; make clean >/dev/null 2>&1; exit 2; }
make clean >/dev/null 2>&1
trap 'make clean >/dev/null 2>&1' EXIT

# A sanitizer report, or any of the aborts a guard raises.
BAD='AddressSanitizer|stack smashing|buffer overflow detected|Assertion .* failed'

report() {                      # $1 = log, $2.. = the invocation
  hits=$((hits+1))
  echo "HIT: $*" | sed 's/^HIT: [^ ]* /HIT: /'
  cp "$1" "$OUT/hit$hits.log"
  sed -n '/ERROR:\|Assertion/,/^SUMMARY\|^$/p' "$1" | head -14 | sed 's/^/    /'
}

if [ "$WHICH" = both ] || [ "$WHICH" = switches ]; then
  echo "== switch surface: tools/switch-matrix.sh under ASan"
  # Start from empty: this file is appended to below, and a leftover one
  # would make a clean tree report the previous run's hits. A check tool
  # that cries wolf is worse than no check tool.
  rm -f "$OUT/switch-hits.txt"
  tools/switch-matrix.sh $BIN > "$OUT/matrix.txt" 2>&1 || true
  # The matrix keeps only two stderr lines per run, so recover each
  # reported invocation by re-running it alone.
  grep -naE "$BAD" "$OUT/matrix.txt" | cut -d: -f1 | while read -r n; do
    inv=$(head -n "$n" "$OUT/matrix.txt" | grep -a '^== ' | tail -1 |
      sed 's/^== //')
    [ -z "$inv" ] && continue
    echo "  re-running for a full trace: $inv"
    # shellcheck disable=SC2086
    env -u DISPLAY timeout 120 $BIN -n $inv _X -od "$OUT/o.as" \
      </dev/null > "$OUT/one.log" 2>&1 || true
    echo "$inv" >> "$OUT/switch-hits.txt"
  done
  if [ -s "$OUT/switch-hits.txt" ]; then
    hits=$((hits + $(wc -l < "$OUT/switch-hits.txt")))
    echo "  switch-surface invocations reported:"
    sed 's/^/    /' "$OUT/switch-hits.txt"
  fi
  runs=$((runs + 529))
fi

if [ "$WHICH" = both ] || [ "$WHICH" = graphics ]; then
  echo "== graphics surface: rendering, which the matrix does not do"
  g() {
    runs=$((runs+1))
    # shellcheck disable=SC2086
    env -u DISPLAY timeout 120 $BIN $CFG $Q "$@" -Xo "$OUT/o.bmp" \
      </dev/null > "$OUT/g.log" 2>&1 || true
    if grep -qaE "$BAD" "$OUT/g.log"; then report "$OUT/g.log" "$@"; fi
    rm -f "$OUT/o.bmp"
  }
  # Every graphics chart mode, bare.
  for m in "" -XX -XX0 -XW -XW0 -XG -XG0 -XP -XP0 -XZ \
           -g -g0 -Z -Z0 -L -L0 -7 -l -d -E -j -8 -5 -k0 -v -w -m -S; do
    g $m
  done
  # Every option, on three chart types that draw very differently.
  for base in "" -XG -XW; do
    for o in "-Xv 0" "-Xv 1" "-Xv 2" "-Xv 3" "-Xv 4" "-Xv 5" "-Xv 6" \
             "-Xv 7" -Xv0 -XA -XL "-XL 1" "-XL 3" "-XL 5" -XU "-XU 0" \
             "-XU 2" "-XU 3" -XUx -XC -XJ -X8 -Xi -Xt -Xu -Xx -Xx0 -Xl \
             -Xe -Xj -XQ -XQ0 -Xr -Xm -X3 -XN -XF "-Xs 100" "-Xs 400" \
             "-XS 100" "-XS 400" "-Xw 200 150" "-Xw 2000 1500" "-X1 5" \
             "-X2 9" "-XI0 0 0" "-XI0 100 3" "-Xk 5" "-Xkv 9" "-XE 1 20" \
             "-XE0 1 5" "-XE3 1 5"; do
      g $base $o
    done
  done
  # The other writers: each is its own output path with its own buffers,
  # and the XBM ones are where the long output path matters.
  for f in -Xb -Xbw -Xbb -Xbp -Xbn -Xbc -Xbv -Xba -XM -XM1 -XM3 -XM6 \
           -XV -Xp -Xp0; do
    g $f; g $f -XG; g $f -g
  done
fi

echo "== $runs invocations, $hits reported"
[ $hits -eq 0 ] || { echo "logs in $OUT"; exit 1; }
exit 0
