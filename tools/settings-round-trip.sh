#!/bin/sh
# Prove the settings save/load pair are inverses, three ways. The console
# build is the host on purpose -- no event loop, it exits, and it loads
# every switch family the GUIs write. "_X" is mandatory: this build IS the
# X11 build, and "=X" in a settings file opens a window and sits there
# (work log item 32). Exits nonzero on any drift.
#
#   make && tools/settings-round-trip.sh
#
# Leg 1: the maintainer's own settings reach a byte-identical fixed point.
# Leg 2: every boolean flag flipped at once must survive a load/save --
#        a flag whose save-twin regresses vanishes or reverts here. The
#        flipped state must itself reach a fixed point, which is what
#        caught the -Yu oscillation and the :YXp0 metric double-convert.
#        Exempt: X (forced by this script's command line), 0b/0n (the -0
#        lockdown family is one-way by design: "_0" does nothing), v3
#        (its boolean rides in the value; the prefix is fixed "="), and
#        bm/bJ, which are gated BY that one-way family -- astrolog.as
#        ships "=0b" and "=0n", so turning the old engine or the JPL web
#        query back on is refused, loudly, by design (work log item 140).
#        Flipping them is asking the program to do what it just forbade.
#        Yu0 normalizes to Yu because the "0" suffix is state, not name.
# Leg 3: tools/settings-fixture.as sets value switches to sentinels; each
#        line's "; EXPECT <regex>" must match the resulting save. A value
#        switch whose save-twin regresses stops matching -- this is what
#        caught -YD losing standard-object renames.
set -e
cd "$(dirname "$0")/.." || exit 1
[ -x ./astrolog ] || { echo "build it first: make"; exit 1; }
T=${TMPDIR:-/tmp}
AST="env -u DISPLAY ./astrolog"
fail=0

# ---- Leg 1: the maintainer's settings are a fixed point ----
$AST -i nrvate.as _X -od "$T/rt-A.as" </dev/null >/dev/null 2>&1
$AST -i "$T/rt-A.as" _X -od "$T/rt-B.as" </dev/null >/dev/null 2>&1
if cmp -s "$T/rt-A.as" "$T/rt-B.as"; then
  echo "leg 1 PASS: settings reach a fixed point after one round trip"
else
  echo "leg 1 FAIL: a second save differs from the first:"
  diff "$T/rt-A.as" "$T/rt-B.as" | head -20
  fail=1
fi

# ---- Leg 2: every flag flipped survives a round trip ----
python3 - "$T/rt-A.as" "$T/rt-F.as" <<'PYEOF'
import re, sys
src, dst = sys.argv[1], sys.argv[2]
EXEMPT = re.compile(r'^[=_](X\b|0b|0n|v3|bp|bm|bJ)')
out = []
for ln in open(src):
    if ln and ln[0] in '=_' and not EXEMPT.match(ln):
        ln = ('_' if ln[0] == '=' else '=') + ln[1:]
    out.append(ln)
open(dst, 'w').write(''.join(out))
PYEOF
$AST -i "$T/rt-F.as" _X -od "$T/rt-G.as" </dev/null >/dev/null 2>&1
tokens() { grep -oE "^[=_][A-Za-z0-9]+" "$1" | sed 's/^\(.\)Yu0$/\1Yu/'; }
tokens "$T/rt-F.as" > "$T/rt-ftok.txt"
tokens "$T/rt-G.as" > "$T/rt-gtok.txt"
if cmp -s "$T/rt-ftok.txt" "$T/rt-gtok.txt"; then
  echo "leg 2 PASS: all flipped flags persisted"
else
  echo "leg 2 FAIL: flipped flags reverted or vanished:"
  diff "$T/rt-ftok.txt" "$T/rt-gtok.txt" | head -20
  fail=1
fi
$AST -i "$T/rt-G.as" _X -od "$T/rt-H.as" </dev/null >/dev/null 2>&1
if cmp -s "$T/rt-G.as" "$T/rt-H.as"; then
  echo "leg 2 PASS: flipped state reaches a fixed point"
else
  echo "leg 2 FAIL: flipped state does not reach a fixed point:"
  diff "$T/rt-G.as" "$T/rt-H.as" | head -20
  fail=1
fi

# ---- Leg 3: sentinel values come back from a save ----
$AST -i tools/settings-fixture.as _X -od "$T/rt-S.as" </dev/null >/dev/null 2>&1
miss=0
grep -oE "; EXPECT .*" tools/settings-fixture.as | sed 's/; EXPECT //' \
  > "$T/rt-expect.txt"
while IFS= read -r rx; do
  grep -Eq "$rx" "$T/rt-S.as" || { echo "leg 3 MISS: $rx"; miss=1; }
done < "$T/rt-expect.txt"
# ---- Leg 3b: the fixture covers every value switch the writer emits ----
# Leg 3 checks the sentinels somebody remembered to write. This checks that
# somebody remembered them all: every "-x"/":x" line in the saved file must
# be named by some EXPECT, or be one of the two spellings that provably
# cannot be set from a settings file. That is the half of T4 that item 140
# fell through -- the -b family was saved and nothing asserted it came
# back, for five days.
#
# Flag switches are not in scope here: they save with "_" and "=" prefixes
# and leg 2 flips every one of them.
# Two exemptions, each measured and each recorded beside its own line in
# tools/settings-fixture.as. "Xb*" because NSwXb() refuses when
# us.fNoWrite is set, which is the state a save runs in; "YXf" because the
# writer emits an aggregate the switch can only set one component at a
# time.
EXEMPT="Xb* YXf"
grep -oE "^[-:][A-Za-z0-9]+" "$T/rt-S.as" | sort -u > "$T/rt-saved.txt"
uncov=0
while IFS= read -r sw; do
  bare=${sw#?}
  skip=0
  for pat in $EXEMPT; do
    case "$bare" in $pat) skip=1;; esac
  done
  [ $skip = 1 ] && continue
  if ! grep -qF -- "$sw" "$T/rt-expect.txt"; then
    echo "leg 3b UNCOVERED: $sw is saved and no EXPECT names it"
    uncov=1
  fi
done < "$T/rt-saved.txt"
if [ $uncov = 0 ]; then
  echo "leg 3b PASS: every value switch the save contains has a sentinel"
else
  echo "leg 3b FAIL: add a fixture line, or exempt it with a measured reason"
  fail=1
fi

if [ $miss = 0 ]; then
  echo "leg 3 PASS: all $(wc -l < "$T/rt-expect.txt") sentinels saved"
else
  fail=1
fi
$AST -i "$T/rt-S.as" _X -od "$T/rt-S2.as" </dev/null >/dev/null 2>&1
if cmp -s "$T/rt-S.as" "$T/rt-S2.as"; then
  echo "leg 3 PASS: fixture state reaches a fixed point"
else
  echo "leg 3 FAIL: fixture state does not reach a fixed point:"
  diff "$T/rt-S.as" "$T/rt-S2.as" | head -20
  fail=1
fi

rm -f "$T/rt-A.as" "$T/rt-B.as" "$T/rt-F.as" "$T/rt-G.as" "$T/rt-H.as" \
  "$T/rt-S.as" "$T/rt-S2.as" "$T/rt-ftok.txt" "$T/rt-gtok.txt" \
  "$T/rt-expect.txt"
[ $fail = 0 ] && echo "PASS: settings round trip, all three legs" \
  || { echo "FAIL: settings round trip"; exit 1; }
