#!/bin/sh
# tools/raw-sprintf-check.sh -- no bare sprintf() in this codebase's own sources.
#
#   tools/raw-sprintf-check.sh              # the check
#   tools/raw-sprintf-check.sh --selftest   # it refuses a planted one
#
# CONVENTIONS.md: sprintf2(S(sz), ...) is the rule. Apple's clang marks bare
# sprintf() deprecated and the release's macOS job fails on it ("No clang
# warnings, ours or vendored"), while glibc's headers and this machine's clang
# say nothing -- so the first anyone knew of two of them, written 2026-09-30,
# was a dry run's macOS leg. The vendored Swiss Ephemeris, cgif, the font
# tools and the server's third-party libraries are not ours and are skipped.
cd "$(dirname "$0")/.." || exit 1
scan() {   # scan <files...>: print offending lines
  grep -HnE '(^|[^A-Za-z0-9_])sprintf[[:space:]]*\(' "$@" 2>/dev/null |
    grep -vE '^[^:]*:[0-9]+:[[:space:]]*(//|/?\*)'
}
if [ "${1:-}" = "--selftest" ]; then
  T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
  printf 'void f(char *s) { sprintf(s, "x"); }\n' > "$T/bad.cpp"
  printf 'void f(char *s) { sprintf2(S(s), "x"); snprintf(s, 1, "x"); }\n' > "$T/good.cpp"
  printf '// sprintf(s) in a comment\n' > "$T/comment.cpp"
  scan "$T/bad.cpp" > /dev/null || { echo "SELFTEST FAIL: a bare sprintf passed"; exit 1; }
  scan "$T/good.cpp" > /dev/null && { echo "SELFTEST FAIL: sprintf2/snprintf flagged"; exit 1; }
  scan "$T/comment.cpp" > /dev/null && { echo "SELFTEST FAIL: a comment flagged"; exit 1; }
  echo "raw-sprintf-check selftest ok"; exit 0
fi
FILES=$(ls *.cpp *.h ephsrv/*.cpp ephsrv/*.h 2>/dev/null |
  grep -vE '^(swe|sweph|swecl|swedate|swehel|swehouse|swejpl|swemmoon|swemplan|swephlib|sweconfig|cgif)' |
  grep -vE '^ephsrv/(uSockets|uWebSockets)')
OUT=$(scan $FILES)
if [ -n "$OUT" ]; then
  echo "bare sprintf() in our own sources (use sprintf2(S(sz), ...)):"
  echo "$OUT"
  exit 1
fi
