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

Exit 0 when every non-"~" ranged row is in the fixture, 1 otherwise.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


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
