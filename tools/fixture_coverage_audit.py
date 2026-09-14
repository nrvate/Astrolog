#!/usr/bin/env python3
"""Every ranged settings switch is exercised by the round-trip fixture.

tools/settings-round-trip.sh has three legs. Leg 2 flips every boolean flag
at once, so a flag whose save-twin regresses cannot hide. Leg 3 checks
value-taking switches against tools/settings-fixture.as, which sets each to
a sentinel and declares what must come back -- but only for the switches
somebody remembered to put in it.

That gap has a name. Work log item 140: the whole -b backend family was
dropped by the settings writer for five days. registry_audit.py could not
see it, because it checks that every spelling the program WRITES resolves
to a row, not that every setting gets written; and the round trip could not
see it either, because the fixture never set those fields.

This closes the half that can be closed cleanly: every row in switch.cpp's
rgswranged[] -- the ranged settings tables, orbs, colours, influences,
rulerships -- must appear in the fixture.

TWO EXEMPTIONS, both measured rather than assumed:

  - The "~" rows are AstroExpression hooks. "-od" does not persist them at
    all, so a settings-file leg structurally cannot verify them
    (QT_TESTING.md says so under the fixture's own description). 54 rows.

  - Rows whose spelling the writer emits under a DIFFERENT name are still
    covered, because the fixture's EXPECT pattern names the saved
    spelling, not the switch. "-YAa 5 5 66.6" comes back as "-Aa 5 66.6".

WHAT THIS DOES NOT COVER, and no longer needs to: rgswitchdef[] has 191
rows, 41 declaring carg>0, of which 31 are absent from the fixture. That
number stopped mattering on 2026-09-02, because the question it was a
proxy for got a better answer. "Takes an argument" is not "is a saved
setting" -- "-x" casts a harmonic chart, "-XI" loads a bitmap, "-YYt"
prints text -- and judging 31 rows by hand was the wrong shape of work.

The writer is the oracle for "is it a setting", so leg 3b of
tools/settings-round-trip.sh asks the question directly: every value
switch that appears in a SAVED settings file must be named by some
EXPECT. Measured against the real output rather than a regex over source,
which is what made it tractable -- 38 uncovered, now 0 with two measured
exemptions. Work log item 172.

AND THE END OF EVERY SPAN, since 2026-09-13. A fixture line that sets one
index near the start of a range says nothing about a writer that stops
before its end, and three did: "-YjA" was written to 18 of 24, "-Y7O" left
out 11-33 and 52-83. Every row must also have a fixture line whose index
range covers the row's iMax, the last index the READER accepts (evaluated
from astrolog.h's constants the way defaults_audit.py does). One row is
excused, below, with its reason; an excuse that is no longer needed fails.

Exit 0 when every non-"~" ranged row is in the fixture and reaches its last
index, 1 otherwise.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from defaults_audit import build_symbols  # noqa: E402

# Rows whose last index the fixture cannot assert yet, each with the reason.
# The audit fails when one of these is covered after all: drop the entry.
SPAN_EXEMPT = {
    'YkA': 'the writer stops at aspect 18, so a sentinel at 24 would fail '
           'leg 3; that gap is fixed on its own branch, which drops this',
}


def main():
    sw = open(os.path.join(ROOT, 'switch.cpp')).read()
    fx = open(os.path.join(ROOT, 'tools', 'settings-fixture.as')).read()

    m = re.search(r'static CONST SWITCHRANGED rgswranged\[\] = \{(.*?)\n\s*\};',
                  sw, re.S)
    if m is None:
        print('cannot find rgswranged[] in switch.cpp -- has it moved or '
              'been renamed? This audit is parsing the registry directly.')
        return 1
    rows = re.findall(r'\{"([^"]*)"', m.group(1))
    hooks = [r for r in rows if r.startswith('~')]
    want = [r for r in rows if not r.startswith('~')]

    covered = set(re.findall(r'^\s*[-:=_]([A-Za-z0-9]+)', fx, re.M))
    missing = [r for r in want if r not in covered]

    # The span leg: some fixture line's index range covers the row's iMax.
    syms = build_symbols(open(os.path.join(ROOT, 'astrolog.h')).read() +
                         '\n' + open(os.path.join(ROOT, 'extern.h')).read())
    spans = {}
    for name, lo, hi in re.findall(
            r'^\s*[-:=_]([A-Za-z0-9]+)\s+(\d+)\s+(\d+)\b', fx, re.M):
        spans.setdefault(name, []).append((int(lo), int(hi)))
    short, stale = [], []
    for name, expr in re.findall(
            r'\{"([^"~][^"]*)",\s*"[^"]*",\s*\w+,\s*\w+,\s*([^,]+),',
            m.group(1)):
        e = re.sub(r'\b([a-zA-Z_]\w*)\b',
                   lambda g: str(syms.get(g.group(1), g.group(1))), expr)
        imax = int(eval(e, {"__builtins__": {}}, {}))
        hit = any(lo <= imax <= hi for lo, hi in spans.get(name, []))
        if name in SPAN_EXEMPT:
            if hit:
                stale.append(name)
            continue
        if not hit:
            short.append((name, imax))
    if short or stale:
        for name, imax in short:
            print('  -%s: no fixture line reaches its last index, %d'
                  % (name, imax))
        for name in stale:
            print('  -%s: reaches its last index now; drop its SPAN_EXEMPT '
                  'entry ("%s")' % (name, SPAN_EXEMPT[name]))
        return 1

    if missing:
        print('ranged settings switches with no fixture line (%d of %d):'
              % (len(missing), len(want)))
        for r in missing:
            print('  -%s' % r)
        print('\nAdd a line to tools/settings-fixture.as setting each to a')
        print('sentinel, with an EXPECT comment naming the pattern that must')
        print('appear in the saved file. Run tools/settings-round-trip.sh to')
        print('see what the writer actually emits -- the saved spelling is')
        print('sometimes not the switch spelling.')
        return 1

    print('fixture coverage: %d of %d ranged switches, %d AstroExpression '
          'hooks exempt' % (len(want), len(rows), len(hooks)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
