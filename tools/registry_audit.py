#!/usr/bin/env python3
"""Cross-check the switch registry against the program's own descriptions.

The registry (astrolog.cpp's rgswflag/rgswranged/rgswitchdef) is the one
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
    """(name, is_prefix) in scan order, parsed from the three tables."""
    s = read('astrolog.cpp')
    rows = []
    for table, pat in [
        (0, r'static CONST SWITCHFLAG rgswflag\[\] = \{(.*?)\};'),
        (1, r'static CONST SWITCHRANGED rgswranged\[\] = \{(.*?)\};'),
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
    ib = s.index('\nflag ', ia + 10)
    body = s[ia:ib]
    out = []
    for m in re.finditer(r'"([^"]*)"', body):
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
