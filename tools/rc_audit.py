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

Also reports any dialog in astrolog.rc with more than one DEFPUSHBUTTON.

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

    # One default button a dialog. The dialog manager honours the LAST
    # DEFPUSHBUTTON it meets, so a second one silently takes Enter away
    # from OK -- dlgCustom and dlgCustomS did that with "Lookup Names" --
    # and rc2qt.py maps both kinds to one control type, so nothing on the
    # Qt side can see it.
    rc = open("astrolog.rc", "rb").read().decode("latin-1")
    two = 0
    for name, body in re.findall(
            r"^(\w+) DIALOG(?:EX)? .*?^BEGIN\r?$(.*?)^END\r?$", rc,
            re.S | re.M):
        n = len(re.findall(r"^\s*DEFPUSHBUTTON\b", body, re.M))
        if n > 1:
            two += 1
            print("%s has %d default buttons" % (name, n))
    print("%d dialog(s) with more than one default button" % two)
    return 1 if bad or two else 0


sys.exit(main())
