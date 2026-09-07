#!/usr/bin/env python3
"""Every context menu entry, against the resource it was transcribed from.

The 42 right-click menus in qtdriver.cpp's CTXITEM tables were derived
from astrolog.rc's menu resources by hand, and nothing kept them in
step. That matters more than it looks, because Windows deliberately
gives one command DIFFERENT labels in different menus -- cmdSecond is
"Print &Nearest Second" on the menu bar and "Print Nearest &Second" in
every text chart's popup -- so the port cannot simply reuse the menu bar
spelling, and where it did, the mnemonic moved to another letter and the
key that works on Windows stopped working here.

Found exactly that on this audit's first run: 18 of the 42 menus, 19
labels, all mnemonic placement.

The existing suite group deliberately says nothing about content -- it
checks that every entry RESOLVES to a menu bar item, which is a
different question and the one that catches a drifted action. This is
the other half.

The second field of a CTXITEM is a menu bar label and is not compared
here: rc_mnemonic_audit.py and the suite's own menu checks own that.

Exit 0 when clean, 1 on any difference.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read(name):
    with open(os.path.join(ROOT, name), newline='', encoding='latin-1') as f:
        return f.read()


def rc_menus():
    """{resource name: [label, ...]} for every menu but the menu bar.

    Separators are skipped rather than compared. They are real content,
    but a moved divider is cosmetic and would make this noisy about the
    one thing it is not for.
    """
    rc = read('astrolog.rc')
    out = {}
    for m in re.finditer(r'^(menu[A-Za-z0-9_]*)\s+MENU\s*$', rc, re.M):
        name = m.group(1)
        if name == 'menu':                     # the menu bar
            continue
        start = m.end()
        nxt = re.search(r'^\S+\s+(MENU|DIALOGEX|DIALOG)\s*$', rc[start:], re.M)
        body = rc[start:start + nxt.start()] if nxt else rc[start:]
        items = []
        for mm in re.finditer(r'MENUITEM\s+(SEPARATOR|"((?:[^"\\]|\\.)*)")',
                              body):
            if mm.group(1) != 'SEPARATOR':
                items.append(mm.group(2).split('\\t')[0])
        out[name] = items
    return out


def qt_menus():
    """{resource name: (table name, [label, ...])}.

    Each table is preceded by a comment naming the Windows menu it came
    from ("Windows' menuV"), which is the only thing tying the two
    together; a table without one is reported as unmatched rather than
    skipped.
    """
    qt = read('qtdriver.cpp')
    out = {}
    for m in re.finditer(r"//[^\n]*Windows' (menu[A-Za-z0-9_]+)[^\n]*\n"
                         r"(?://[^\n]*\n)*"
                         r"static CONST CTXITEM (rgctx\w+)\[\] = \{(.*?)\n?\s*\};",
                         qt, re.S):
        labels = [mm.group(1) for mm in
                  re.finditer(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,', m.group(3))]
        out[m.group(1)] = (m.group(2), labels)
    return out


def main():
    rc, qt = rc_menus(), qt_menus()
    if len(rc) < 40 or len(qt) < 40:
        print(f"FAIL: parsed {len(rc)} resource menus and {len(qt)} Qt "
              f"tables; both should be 42")
        return 1
    bad = 0
    for name in sorted(rc):
        if name not in qt:
            print(f"MISSING {name}: no Qt context menu table")
            bad += 1
            continue
        tbl, qitems = qt[name]
        ritems = rc[name]
        for i in range(max(len(qitems), len(ritems))):
            a = ritems[i] if i < len(ritems) else '(nothing)'
            b = qitems[i] if i < len(qitems) else '(nothing)'
            if a != b:
                print(f"DIFFER {name} ({tbl}) entry {i}:")
                print(f"    astrolog.rc  \"{a}\"")
                print(f"    qtdriver.cpp \"{b}\"")
                bad += 1
    for name in sorted(qt):
        if name not in rc:
            print(f"EXTRA {qt[name][0]}: names {name}, which is not a "
                  f"menu resource")
            bad += 1
    if bad:
        print(f"FAIL: {bad} context menu difference(s)")
        return 1
    print(f"OK: {len(rc)} context menus match astrolog.rc entry for entry "
          f"({sum(len(v) for v in rc.values())} labels)")
    return 0


if __name__ == '__main__':
    sys.exit(main())
