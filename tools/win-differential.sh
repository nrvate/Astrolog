#!/bin/sh
# Diff what Windows computes against what Linux computes, with no display.
#
#   tools/win-differential.sh [outdir]
#
# QT_CI_PLAN.md item 6.4b. The Windows build is this fork's behavioural
# oracle -- parity with it is the spec -- and until 2026-09-01 comparing
# the two builds' text output meant driving a real window under Wine with
# Xvfb, a window manager and xdotool, at tens of seconds an invocation.
#
# It does not. astrolog.exe has no console entry point (main() is inside
# #ifndef WIN; the WIN build enters at WinMain), but astrolog-wcli.exe
# does: WCLI is not WIN, so the same shared core compiled by the same
# mingw for the same target runs from a command line. The whole
# 71-invocation chart matrix then runs under Wine with no display, no
# window manager and no input simulation.
#
# WHAT IT COVERS AND WHAT IT DOES NOT. It exercises the shared core as
# compiled for Windows -- which is most of the value, since that is where
# a portability difference would live. It compiles neither wdriver.cpp nor
# wdialog.cpp, so it says nothing about the Windows GUI; that is item 6.3
# and QT_COMPARING_WITH_WINDOWS.md. A complement to the oracle, never a
# replacement.
#
# TWO NORMALISATIONS, both earned rather than assumed.
#
# -z0 0, prepended to every invocation on BOTH sides, because "-z0
# Autodetect" is broken in both builds and in two different ways. This
# harness found it; the pin is what lets it see anything else.
#
# Measured 2026-09-02, same chart, transits at a January date and a July
# date:
#
#   Linux    DT Zone 8W   DT Zone 8W     always daylight time
#   Windows  ST Zone 8W   ST Zone 8W     never daylight time
#
# Neither answers "was that date in daylight time". general.cpp:2346
# splits the autodetection on #ifdef PC into two implementations that can
# only agree by coincidence. The PC side (WIN, WCLI) compares
# GetSystemTime() against GetLocalTime() and sets is.fDst from the host
# machine's CURRENT clock offset -- the answer to "is it summer here
# today", not "was that chart date in daylight time". The non-PC side
# (console, Qt) does the right thing, looking the location up in the atlas
# and consulting the timezone-change database -- and then throws the
# result away: "is.fDst = (dst > 0.0)" where dst is still dstAuto, which
# is 24.0, so it is unconditionally true.
#
# Left unpinned it cascades: 210 differing lines of 7,071, all of them
# downstream of one "Transits at:" header, in the -Tt and -p sections
# only. Reported rather than fixed here -- it is a shared-core behaviour
# change and the maintainer's call, not a CI harness's.
#
# The output path. Wine sees the tree as Z:\..., so anything that prints
# its own path differs in syntax alone. Filtered by shape, not by
# blanking whole lines, so a real difference in such a line still shows.
#
# Both sides run through a wrapper so the prepended switch is identical
# and chart-matrix.sh sees a single executable word, as it requires. The
# wrapper's own location is irrelevant: Astrolog resolves its data from
# the real binary's directory, and both binaries live in the tree.
set -eu

out=${1:-out/win-diff}
cd "$(dirname "$0")/.."
root=$(pwd)

command -v wine >/dev/null 2>&1 || {
  echo "NO WINE: install wine to run the Windows differential."; exit 1; }
[ -x "$root/astrolog" ] || { echo "build ./astrolog first: make"; exit 1; }
[ -f "$root/astrolog-wcli.exe" ] || {
  echo "build ./astrolog-wcli.exe first: make wcli"; exit 1; }

mkdir -p "$out"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# ~887,000 files through Wine's path translation looks exactly like the
# app hanging, so the Windows side must never be pointed at /swe. Both
# sides are pinned to the bundled ephemeris for the same reason CI is.
cat >"$tmp/lin" <<EOF
#!/bin/sh
exec "$root/astrolog" -Yi1 "$root/ephem" -z0 0 "\$@"
EOF
# No 2>/dev/null here, though the temptation is real: Wine prints its own
# noise on some systems. It prints none on this one, and discarding the
# stream discards ASTROLOG's diagnostics with it -- which showed up
# immediately as "SwissEph file not found" and "The Campanus system of
# houses is not defined at extreme latitudes" appearing on the Linux side
# and nowhere on the Windows side. That is a harness difference dressed as
# a parity finding, and it is exactly what this harness exists to avoid.
# If a machine does emit Wine noise, filter it by shape in normalize()
# below -- it is tagged, e.g. "0024:fixme:" -- never by dropping stderr.
cat >"$tmp/win" <<EOF
#!/bin/sh
exec wine "$root/astrolog-wcli.exe" -Yi1 "$root/ephem" -z0 0 "\$@"
EOF
chmod +x "$tmp/lin" "$tmp/win"

# Path syntax only: Z:\... against /..., and the drive-letter form Wine
# reports. Rewritten to a marker rather than deleted, so a line that
# differs for any OTHER reason still differs.
normalize() {
  sed -e "s|not found in PATH '.*'|not found in PATH <paths>|" \
      -e "s|$root|ROOT|g" \
      -e 's|Z:\\[^'"'"'"]*|WINPATH|g' \
      -e 's|;:[^'"'"'";]*|;WINPATH|g' \
      -e 's|\r$||'
}

echo "== linux"
tools/chart-matrix.sh "$tmp/lin" 2>&1 | normalize >"$out/linux.raw"
echo "== windows (wine)"
tools/chart-matrix.sh "$tmp/win" 2>&1 | normalize >"$out/windows.raw"

# Diagnostics are compared as a SET, chart output as a sequence.
#
# chart-matrix.sh merges stderr into stdout, and where a stderr line lands
# relative to the surrounding stdout is a property of the C runtime's
# buffering -- glibc's and msvcrt's differ. Measured: the single
# "SwissEph file 'sepm9401.se1' not found" line that both builds emit
# appears before the chart header on Linux and after it under Wine, which
# diffed as two changed lines and was the entire remaining difference
# across 7,070. That is not a parity finding and dressing it as one would
# make this harness cry wolf on every run.
#
# So the diagnostics come out of the sequence and are counted instead --
# the same file, missing the same number of times, on both sides. The
# fact of the diagnostic is still compared; only its position is not.
# grep -a, not grep. Chart output carries IBM line-drawing bytes, so grep
# calls these files binary and prints "binary file matches" instead of the
# lines -- a trap QT_TESTING.md already records, met here live.
for f in linux windows; do
  grep -a    'not found in PATH' "$out/$f.raw" | sort >"$out/$f.diag" || true
  grep -av   'not found in PATH' "$out/$f.raw"        >"$out/$f.txt"
done
if ! diff -u "$out/linux.diag" "$out/windows.diag" >"$out/diag.diff" 2>&1; then
  echo "== the two builds report DIFFERENT missing ephemeris files:"
  cat "$out/diag.diff" | sed 's/^/   /'
  exit 1
fi
echo "== diagnostics identical as a set ($(wc -l <"$out/linux.diag") lines)"

nl=$(wc -l <"$out/linux.txt"); nw=$(wc -l <"$out/windows.txt")
echo "== $nl lines linux, $nw lines windows"

# Vacuity: a harness whose invocations all error diffs to zero and reads
# exactly like a proof. chart-matrix.sh shipped its first draft with 15 of
# 70 invocations failing on wrong switch arity.
for f in linux windows; do
  bad=$(grep -cE 'Too few parameters|Unknown switch|Bad parameter|illegal' \
        "$out/$f.txt" || true)
  [ "$bad" -eq 0 ] || {
    echo "VACUOUS: $bad error-shaped lines in $f.txt -- this is not a"
    echo "== clean result, it is a harness that did not run."; exit 1; }
  [ "$(wc -l <"$out/$f.txt")" -gt 5000 ] || {
    echo "VACUOUS: $f.txt is only $(wc -l <"$out/$f.txt") lines."; exit 1; }
done

if diff -u "$out/linux.txt" "$out/windows.txt" >"$out/win.diff" 2>&1; then
  echo "== identical: Windows and Linux compute the same text charts"
  rm -f "$out/win.diff"
  exit 0
fi
nd=$(grep -c '^[-+][^-+]' "$out/win.diff" || true)
echo "== $nd differing lines of $nl -- $out/win.diff"
head -30 "$out/win.diff" | sed 's/^/   /'
exit 1
