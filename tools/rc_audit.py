#!/usr/bin/env python3
"""
Check that every control a transcribed dialog builds is actually wired up.

RcBuildDialogQt() creates whatever the resource table lists, so a control
that nothing looks up still appears on screen -- it just does nothing. That
is easy to miss by eye and easy to introduce, either by writing a dialog
against a table that was incomplete at the time, or by adding to the
resource later.

Reports any symbol in a dialog's table that qtdialog.cpp never mentions.
IDC_STATIC (labels and group boxes), IDOK, IDCANCEL, the icon, and the dx
checkbox runs the restriction dialogs address by index are all expected to
go unnamed.

Usage:  python3 tools/rc_audit.py
"""

import re
import sys

IGNORE = {"IDC_STATIC", "IDOK", "IDCANCEL", "icon", "dx", ""}


def main():
    hdr = open("qtrcdlg.h", encoding="utf-8").read()
    src = open("qtdialog.cpp", encoding="utf-8", newline="").read()
    tables = re.findall(r"rgctl(\w+)\[\] = \{(.*?)\n\};", hdr, re.S)
    bad = 0
    for name, body in tables:
        if ("rgctl%s," % name) not in src:
            continue                      # not transcribed yet
        ctls = re.findall(
            r'\{(ctl\w+),\s*"(?:[^"]*)",\s*"([^"]*)",\s*(-?\d+),', body)
        syms = {}
        for kind, sym, idx in ctls:
            if sym in IGNORE:
                continue
            syms.setdefault(sym, set()).add(int(idx))
        missing = [s for s in sorted(syms) if ('"%s"' % s) not in src]
        status = ", ".join(missing) if missing else "ok"
        if missing:
            bad += 1
        print("%-10s %3d controls   %s" % (name, len(ctls), status))
    print("\n%d dialog(s) with unwired controls" % bad)
    return 1 if bad else 0


sys.exit(main())
