#!/usr/bin/env python3
#
# Astrolog (Version 8.00) File: tools/rc_mnemonic_audit.py
#
# Check that every menu label in qtdriver.cpp puts its "&" mnemonic on the
# same character Windows does.
#
# A Windows user drives these menus by muscle memory -- Alt+C then "x" for
# Standard Radix, Alt+C then "g" for Aspect Midpoint Grid. Those keys come
# from the "&" placement in astrolog.rc, which is often *not* the first
# letter (upstream moved them around to keep each menu's mnemonics unique).
# Picking the obvious letter instead silently breaks that muscle memory, and
# nothing else in the build notices, so this is checked here.
#
# Compares by the label with "&" and any "\t" accelerator stripped, so it
# only ever flags a mnemonic that moved -- never a wording difference.
#
# Usage, from the repo root:
#
#   python3 tools/rc_mnemonic_audit.py
#
# Exits nonzero if any mnemonic diverges.

import io
import re
import sys

RC = "astrolog.rc"
QT = "qtdriver.cpp"


def strip(sz):
    """The label with its accelerator and mnemonic marker removed."""
    return sz.split("\\t")[0].replace("&", "").strip()


def RgrcItems():
    """Map of stripped label -> Windows label, for the main menu bar."""
    rc = io.open(RC, encoding="latin-1", newline="").read()
    m = re.search(r"^menu MENU\r?\n(.*?)^END\r?\n", rc, re.S | re.M)
    if m is None:
        sys.exit("%s: no 'menu MENU' block found" % RC)
    rgrc = {}
    for sz in re.findall(r'(?:MENUITEM|POPUP)\s+"((?:[^"\\]|\\.)*)"', m.group(1)):
        if sz == "SEPARATOR":
            continue
        szKey = strip(sz)
        if szKey:
            # First spelling wins: a label repeated across menus is the same
            # item, and upstream keeps the mnemonic consistent for those.
            rgrc.setdefault(szKey, sz.split("\\t")[0].rstrip())
    return rgrc


def main():
    rgrc = RgrcItems()
    qt = io.open(QT, encoding="latin-1", newline="").read()

    # Every string literal in the file, not just the ones in Add*Action()
    # calls -- the context menu and hotkey tables name menu items by label
    # too, and those have to track the same spelling.
    rgbad, cok = [], 0
    for sz in re.findall(r'"((?:[^"\\\n]|\\.)*)"', qt):
        if "&" not in sz:
            continue
        szWant = rgrc.get(strip(sz))
        if szWant is None:
            continue
        if szWant == sz:
            cok += 1
        elif (sz, szWant) not in rgbad:
            rgbad.append((sz, szWant))

    print("%d menu labels checked against %s, %d mismatched" %
          (cok + len(rgbad), RC, len(rgbad)))
    for sz, szWant in rgbad:
        print("  %-34s should be  %s" % (sz, szWant))
    return 1 if rgbad else 0


if __name__ == "__main__":
    sys.exit(main())
