#!/bin/sh
# Windows-specific behaviour the Linux nets structurally cannot see.
#
#   tools/win-console-checks.sh [binary]
#
# Everything here is about how the shared core handles PATHS when compiled
# for Windows, because that is where the platforms actually differ: a
# different CRT, a different stat(), drive letters, and a different
# directory separator. It runs astrolog-wcli.exe under Wine with no
# display -- the same trick tools/win-differential.sh uses -- so it costs
# seconds rather than the minutes a real window needs.
#
# WHY THIS EXISTS. tools/win-differential.sh diffs the 71-invocation CHART
# matrix between the two builds. That is a real net and it covers the
# computation, but it renders charts: it never asks what the ephemeris
# SEARCH PATH ends up containing. The switch matrix, which does exercise
# that surface, is Linux-only and cannot simply be diffed across platforms
# because half its output is absolute paths that differ by construction.
#
# So the path surface had no Windows coverage at all, and a real bug went
# out in it: an explicit "-Yi1 M:\swe" was silently dropped because the
# path builder ran stat() on it, and Windows' stat() fails on a bare drive
# letter and, per MSVC's documentation, on a trailing backslash for
# anything but a root. Reported by a user, on Windows, after every Linux
# net passed. See QT_GUI_PLAN.md work log item 179.
#
# These are assertions about behaviour, not a differential: each one says
# what the right answer is, so a wrong answer is wrong rather than merely
# different from yesterday.
set -eu

bin=${1:-./astrolog-wcli.exe}
[ -f "$bin" ] || { echo "no such binary: $bin -- run 'make wcli' first"; exit 2; }
command -v wine >/dev/null || { echo "wine not found"; exit 2; }

fail=0
# printf, never echo. Every string here is a Windows path, and echo
# interprets the backslash escapes in one: "C:\nosuchdir" prints as "C:",
# a newline, then "osuchdir", and "Astrolog\font" loses the "f" to a
# formfeed. The first run of this script reported a failure whose message
# was itself unreadable for exactly that reason. This is the third time a
# backslash in an echo has cost time in this project -- see the "\a" bell
# in package-macos.sh and in ci-verify-windows-installer.sh.
ok()   { printf '  ok: %s\n' "$1"; }
bad()  { printf '  FAIL: %s\n' "$1"; fail=1; }

# The ephemeris path as Swiss reports it back. Asking for a body whose
# file is deliberately absent is what makes Swiss print the path at all --
# the diagnostic is the only place the assembled path is observable from
# outside the process.
epath() {
  WINEDEBUG=-all wine "$bin" "$@" \
    -qa 6 15 1990 12:00 0 122W19 47N36 -R1 _X 2>&1 \
    | grep -m1 "not found in PATH" | sed "s/.*PATH '//;s/'.*//"
}

echo "== Windows path handling ($bin under Wine)"

# 1. THE REGRESSION. An explicit -Yi is an instruction. It goes in the
#    path whether or not stat() can see it, because a user who mistypes
#    one has to be able to find it in the diagnostic.
p=$(epath -Yi1 'C:\nosuchdir')
case $p in
  *'C:\nosuchdir'*) ok "a -Yi directory that does not exist is still searched" ;;
  *) bad "'-Yi1 C:\\nosuchdir' never reached the path: [$p]" ;;
esac

# 2. A path with a drive letter is absolute and must NOT be rewritten
#    relative to the executable. "C:\x" becoming
#    "C:\Program Files\Astrolog\C:\x" is the failure this catches.
#
#    Stated POSITIVELY, as ";C:\nosuchdir" -- the entry preceded by the
#    separator rather than by a backslash. The first draft asked instead
#    that "\C:\nosuchdir" be absent, which is vacuously true when the
#    directory is absent altogether: it passed cleanly under a sabotage
#    that deleted the entry, which is precisely the "a harness whose
#    invocations all fail still reads as a proof" trap.
case $p in
  *';C:\nosuchdir'*) ok "a drive-lettered -Yi is left absolute" ;;
  *) bad "a drive-lettered path is not its own path entry: [$p]" ;;
esac

# 3. The complement: a path with no drive letter IS relative to the
#    executable, not to the working directory. Documented behaviour that
#    a bundle layout depends on -- see SwissEnsurePath().
p2=$(epath -Yi1 'relephem')
case $p2 in
  *':\'*'relephem'*) ok "a relative -Yi is resolved against the executable" ;;
  *) bad "'-Yi1 relephem' was not made exe-relative: [$p2]" ;;
esac

# There is deliberately no separator check here. It was written, and it
# passed under a sabotage that reintroduced the exact bug it named -- so
# it was worthless, and the sabotage is the only reason that is known.
#
# The reason is that PATH_SEPARATOR is ";:" on Unix (a character class,
# "semicolon or colon may be used") and ";" alone on Windows. Joining with
# the whole string is a real defect, but only on Unix; on Windows the
# broken and the correct code emit the same single character. An assertion
# that cannot fail on the platform it runs on is worse than no assertion,
# because it reads like coverage. It lives in run-qt-tests.sh instead,
# where PATH_SEPARATOR is two characters and the sabotage does move it.

[ $fail -eq 0 ] || { echo "FAIL: Windows path handling"; exit 1; }
echo "PASS: Windows path handling"
