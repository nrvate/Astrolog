#!/usr/bin/env python3
"""Check that every by-name control lookup can actually find its control.

    python3 tools/rc_lookup_audit.py

rc2qt.py splits a trailing run of digits off a resource symbol into a
separate index, so astrolog.rc's dbRe_R0, dbRe_R1 and dbRe_R all reach
qtrcdlg.h as szId "dbRe_R" with nIdx 0, 1 and -1. A lookup by bare name
means "the one whose symbol carried no digits", i.e. nIdx -1.

Two ways that goes wrong, neither visible to the other audits here, which
read the tables as text and cannot see what got bound:

  * The bare name matches nothing, because every control with that symbol
    is indexed. PwRcFindQt() returns NULL, the caller skips it, and the
    control is simply never wired -- no warning, no crash.

  * The bare name is ambiguous. Before this was fixed PwRcFindQt() matched
    on the symbol alone and returned whichever the generated table listed
    first, which put "Toggle Minors" and "Toggle &Majors" on no button at
    all while their Restrict All buttons silently ran the toggle too, and
    bound us.fEquator to the "Equatorial Latitudes" box next to its own.
    Ambiguity is now resolved by matching nIdx, so this half is reported
    for information: those symbols are the trap, and a reordered resource
    is what used to change the answer.
"""

import re
import sys
import collections

ENTRY = re.compile(r'\{ctl\w+,\s*(?:"(?:[^"\\]|\\.)*"|NULL),\s*"([^"]+)",\s*(-?\d+)')
BARE = re.compile(r'PwRcFindQt\(\s*\w+\s*,\s*"([^"]+)"')
NEG = re.compile(r'\{"([^"]+)",\s*-1,')
# Symbols the port synthesises rather than taking from the resource.
SYNTHETIC = {"IDOK", "IDCANCEL"}


def main():
    with open("qtrcdlg.h", newline="") as f:
        table = f.read()
    with open("qtdialog.cpp", newline="") as f:
        src = f.read()

    byid = collections.defaultdict(list)
    for sid, idx in ENTRY.findall(table):
        byid[sid].append(int(idx))

    wanted = (set(BARE.findall(src)) | set(NEG.findall(src))) - SYNTHETIC

    missing = sorted(s for s in wanted if s in byid and -1 not in byid[s])
    unknown = sorted(s for s in wanted if s not in byid)
    for s in missing:
        print("UNFINDABLE %-14s only indexed %s -- a bare lookup finds nothing"
              % (s, sorted(byid[s])))
    for s in unknown:
        print("UNKNOWN    %-14s no such control in qtrcdlg.h" % s)

    shared = sorted(s for s in wanted
                    if s in byid and -1 in byid[s] and len(byid[s]) > 1)
    print("%d by-name lookup(s) checked, %d that cannot resolve"
          % (len(wanted), len(missing) + len(unknown)))
    if shared:
        print("%d symbol(s) where a bare name sits beside indexed controls, "
              "and the index is what tells them apart: %s"
              % (len(shared), " ".join(shared)))
    return 1 if (missing or unknown) else 0


sys.exit(main())
