#!/bin/sh
# Compile both Qt builds with clang, as the macOS release job does, and fail
# on any clang warning -- locally, before a tag.
#
#   tools/clang-gate.sh            # both Qt builds, newest clang++ found
#   CLANG_CXX=clang++-14 tools/clang-gate.sh
#
# The macOS job builds "make qt" and "make qt-test" with the makefiles' own
# flags, where g++ is Apple clang, and fails on any warning at all
# (tools/ci-assert-clang-clean.sh build.log 0). GCC reads the same source
# differently, and the Linux nets never saw that class: v8.00-qt.19's first
# run failed there on arrays sized by a pointer difference, which clang
# folds to a constant with a -Wgnu-folding-constant warning and MSVC rejects
# outright, while g++ said nothing. That surfaced an hour after the tag.
#
# So this gate is the macOS job's, plus exactly one warning class: the
# makefiles' own CPPFLAGS, no -Wall, the same assertion script, the same
# zero -- and the variable-length-array warning named explicitly, because a
# gate on clang's defaults was measured BLIND to the very construct that
# failed .19. Its first draft reported clean with that code planted back in.
# The macOS runner's Apple clang (LLVM 17 era) folds such an array to a
# constant and warns by default (-Wgnu-folding-constant); clang 22 and clang
# 14 here say nothing by default, and clang 18+ reports the construct only as
# -Wvla-cxx-extension (older clang: -Wvla-extension). MSVC rejects it
# outright, so it is the one class both other release lanes fail on.
#
# Objects only, never linked, into a cache outside the tree, so a run with
# nothing changed is quick and no binary in the tree is touched. The
# mechanism is tools/warning_audit.py --cached's, for the same reasons: the
# makefiles compile with a literal g++ that no variable can swap, so a
# second makefile replaces the pattern rule (an identical target and
# prerequisite list replaces the earlier rule) and keeps each object's
# compiler output beside it; the cache is per checkout, because make compares
# times in its own tree and a shared cache let one worktree's objects hide
# another's edit; and the key covers the compiler version, the makefile,
# Makefile.srcs and the rule. An object with no output file stops the run --
# if the rule ever stopped replacing, g++ would build silently and this would
# read clean.
#
# The touch in the rule is load bearing. clang writes a .d and its .o in the
# same clock tick, and when the .d lands even a millisecond later,
# Makefile.qt's "$(OBJS): %.o: %.d" reads that object as out of date FOREVER
# -- the gate recompiled a random subset of the cache on every run, 20-40 s
# each time, for a cache whose whole point is not compiling. g++ writes the
# .d first, which is why warning_audit's cache and every tree build never
# had this.
#
# No clang at all is a skip, not a failure: a release runner or a user's
# machine may not have one, and the macOS job is the gate of record there.
set -e
cd "$(dirname "$0")/.." || exit 1
# Canonical spelling, because the cache root is keyed on this string and
# this checkout answers to two: /shares/Astrolog and, through the symlink,
# /nvmraid/shares/Astrolog. Two spellings meant two caches, and a gate run
# from each in turn recompiled everything, every time.
ROOT=$(pwd -P)

CXX=${CLANG_CXX:-}
if [ -z "$CXX" ]; then
  for c in $(ls /usr/bin/clang++-[0-9]* 2>/dev/null | sed 's/.*clang++-//' | sort -n -r) ; do
    CXX=clang++-$c; break
  done
  [ -n "$CXX" ] || { command -v clang++ >/dev/null 2>&1 && CXX=clang++; }
fi
if [ -z "$CXX" ] || ! command -v "$CXX" >/dev/null 2>&1; then
  echo "clang gate: skipped, no clang++ on this machine"
  exit 0
fi
# Whichever spelling of the VLA warning this clang knows.
VLAFLAG=
for f in -Wvla-cxx-extension -Wvla-extension; do
  if printf 'int x;\n' | "$CXX" -x c++ "$f" -Werror=unknown-warning-option \
      -fsyntax-only - >/dev/null 2>&1; then
    VLAFLAG=$f; break
  fi
done
JOBS=${CLANG_GATE_JOBS:-4}
[ "$JOBS" -le 4 ] 2>/dev/null || JOBS=4

RULE='$(OBJDIR)/%.o: %.cpp | $(OBJDIR)
	$(CLANGGATECXX) $(CPPFLAGS) $(CLANGGATEFLAGS) -c -o $@ $< >$@.warn 2>&1; s=$$?; test $$s -eq 0 && touch $@; cat $@.warn; exit $$s
'
root_key=$(printf '%s' "$ROOT" | sha256sum | cut -c1-12)
BASE=${CLANG_GATE_CACHE:-$HOME/.cache/astrolog-clang-gate}/$root_key
mkdir -p "$BASE"
LOG=$BASE/clang.log
: > "$LOG"

for mk in Makefile.qt Makefile.qt.test; do
  key=$( { "$CXX" --version; printf '%s\n' "$VLAFLAG"; cat "$mk" Makefile.srcs; printf '%s' "$RULE"; } \
    | sha256sum | cut -c1-16)
  dir=$BASE/$mk-$key
  for old in "$BASE/$mk"-*; do
    [ -d "$old" ] && [ "$old" != "$dir" ] && rm -rf "$old"
  done
  mkdir -p "$dir"
  printf '%s' "$RULE" > "$dir/rule.mk"
  objs=$(make --no-print-directory -f "$mk" OBJDIR="$dir" \
    --eval='clanggateobjs:;@echo $(OBJS)' clanggateobjs)
  [ -n "$objs" ] || { echo "clang gate: no object list from $mk"; exit 2; }
  if ! make --no-print-directory -f "$mk" -f "$dir/rule.mk" -j"$JOBS" \
      CLANGGATECXX="$CXX" CLANGGATEFLAGS="$VLAFLAG" OBJDIR="$dir" $objs \
      >"$dir/make.out" 2>&1; then
    echo "clang gate: $mk does not compile with $CXX"
    grep -E ': error:|^make.*\*\*\*' "$dir/make.out" | head -10
    exit 2
  fi
  for o in $objs; do
    [ -f "$o.warn" ] || {
      echo "clang gate: $o has no compiler output kept -- the replacement"
      echo "rule did not replace $mk's, so nothing here was read."
      exit 2; }
    cat "$o.warn" >> "$LOG"
  done
done

echo "clang gate: $("$CXX" --version | head -1), ${VLAFLAG:-no VLA warning known}"
tools/ci-assert-clang-clean.sh "$LOG" 0
