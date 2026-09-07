#!/usr/bin/env python3
"""Cross-check the switch registry against the program's own descriptions.

The registry (switch.cpp's rgswflag/rgswranged/rgswtilde/rgswitchdef) is the one
place a switch spelling exists. Two other places still describe
spellings by hand and can drift: the -H help text (DisplaySwitches* in
charts0.cpp and xscreen.cpp) and the settings writer (FOutputSettings()
in io.cpp). This audit extracts every spelling those describe and
resolves each against the registry using the dispatch's own rule --
exact match, else a prefix row -- so a documented or written spelling
with no row fails loudly.

Exit 0 when clean, 1 on any unresolved spelling.
"""

import re
import sys
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read(name):
    with open(os.path.join(ROOT, name), newline='') as f:
        return f.read()


def registry_rows():
    """(name, is_prefix) in scan order, parsed from the four tables
    in switch.cpp (the registry moved there from astrolog.cpp in
    phase 2's P1; the tilde table joined in P4)."""
    s = read('switch.cpp')
    rows = []
    for table, pat in [
        (0, r'static CONST SWITCHFLAG rgswflag\[\] = \{(.*?)\};'),
        (1, r'static CONST SWITCHRANGED rgswranged\[\] = \{(.*?)\};'),
        (3, r'static CONST SWITCHTILDE rgswtilde\[\] = \{(.*?)\};'),
        (2, r'static CONST SWITCHDEF rgswitchdef\[\] = \{(.*?)\};')]:
        body = re.search(pat, s, re.S).group(1)
        for m in re.finditer(r'\{"([^"]*)",\s*([^,]+),', body):
            name, second = m.group(1), m.group(2).strip()
            rows.append((name, table == 2 and 'grfSwPrefix' in second))
    return rows


def resolves(name, rows):
    for rowname, isprefix in rows:
        if (name.startswith(rowname) if isprefix else name == rowname):
            return True
    return False


def help_spellings():
    """Every switch token the -H texts lead a line with."""
    out = []
    for src in ('charts0.cpp', 'xscreen.cpp'):
        s = read(src)
        for m in re.finditer(r'Print[SZ]?\w*\(\s*\r?\n?\s*"(\s*)([_=-])'
                             r'([A-Za-z0-9~;@.>+]+)', s):
            if len(m.group(1)) in (1, 2):     # help lines indent one space
                out.append((src, m.group(3)))
    return out


def writer_spellings():
    """Every switch token FOutputSettings() emits."""
    s = read('io.cpp')
    ia = s.index('flag FOutputSettings()')
    # The function's own closing brace, not the next "flag " at column 0:
    # every function between this one and FInputData() is static or returns
    # something else, so the old bound swallowed 200 lines of NParseSz() and
    # its comments. Harmless while the quote parity happened to line up, and
    # not once it did not -- it invented "1;" and "4." out of ordinary C.
    ib = s.index('\n}\n', ia)
    body = s[ia:ib]
    out = []
    # A C string, escapes included, so a \" inside one does not end it.
    for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', body):
        text = m.group(1)
        for t in re.finditer(r'(?:^|\\n|\s)[-=_:](?:%c)?'
                             r'([A-Za-z0-9~;@.>+]+)\s', text):
            tok = t.group(1)
            if not re.match(r'^\d+$', tok):   # skip bare numbers
                out.append(('io.cpp', tok))
    # %cXx-style flag emissions: sprintf(sz, "%cXx ...", ChDashF(...))
    for m in re.finditer(r'"%c([A-Za-z0-9]+)[ \\]', body):
        out.append(('io.cpp', m.group(1)))
    return out


def main():
    rows = registry_rows()
    if len(rows) < 240:
        print(f"FAIL: parsed only {len(rows)} registry rows")
        return 1
    bad = 0
    seen = set()
    for src, name in help_spellings() + writer_spellings():
        if name in seen:
            continue
        seen.add(name)
        if not resolves(name, rows):
            print(f"MISSING {src}: documented/written spelling "
                  f"\"{name}\" resolves to no registry row")
            bad += 1
    if bad:
        print(f"FAIL: {bad} unresolved spelling(s) "
              f"({len(seen)} checked, {len(rows)} rows)")
        return 1
    print(f"OK: registry audit clean -- {len(seen)} documented/written "
          f"spellings all resolve against {len(rows)} rows")
    return 0


if __name__ == '__main__':
    sys.exit(main())
