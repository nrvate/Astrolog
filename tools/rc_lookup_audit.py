#!/usr/bin/env python3
"""Check that every by-name control lookup resolves, in the dialog using it.

    python3 tools/rc_lookup_audit.py

rc2qt.py splits a trailing run of digits off a resource symbol into a
separate index, so astrolog.rc's dbRe_R0, dbRe_R1 and dbRe_R all reach
qtrcdlg.h as szId "dbRe_R" with nIdx 0, 1 and -1. A lookup by bare name
means "the one whose symbol carried no digits", i.e. nIdx -1.

Scope is per dialog, and that is the whole point. qtrcdlg.h holds all 24
dialogs concatenated, and the same symbols recur across them -- dx01 and
deo01 belong to several. A lookup runs against one dialog's slice, so a
check that asks only whether a symbol exists *somewhere* in the table
passes on nearly anything. Each lookup here is resolved against the
controls of the dialog whose array the calling function builds.

Two failures, neither visible to the other audits, which read the tables
as text and cannot see what got bound:

  * The lookup matches nothing in its own dialog. PwRcFindQt() returns
    NULL, the caller skips it, and the control is never wired -- no
    warning, no crash, just a dead checkbox.

  * It matches more than one. That cannot happen today, and the audit
    fails if it ever does.

Symbols where a bare name sits beside indexed controls *in the same
dialog* are reported for information. They are the trap this came from:
PwRcFindQt() used to match on szId alone and returned whichever the table
listed first, which left "Toggle Minors" and "Toggle &Majors" wired to
nothing while their Restrict All buttons silently ran the toggle too, and
bound us.fEquator to the "Equatorial Latitudes" box beside its own. The
lookup matches nIdx now, so they resolve by construction -- but they are
the rows where the index is load bearing.
"""

import re
import sys
import collections

ARRAY = re.compile(r'static CONST RCCTL (rgctl\w+)\[\] = \{(.*?)\n\};', re.S)
ENTRY = re.compile(r'\{ctl\w+,\s*(?:"(?:[^"\\]|\\.)*"|NULL),\s*"([^"]+)",\s*(-?\d+)')
# A function definition at column 0, through to the next one.
FUNC = re.compile(r'^(?:[A-Za-z_][\w *]*?)\b(\w+Qt)\s*\([^;]*?\)\s*\n\{', re.M)
USES = re.compile(r'\brgctl(\w+)\b')
BARE = re.compile(r'PwRcFindQt\(\s*\w+\s*,\s*"([^"]+)"')
IDXD = re.compile(r'PwRcFindIdxQt\(\s*\w+\s*,\s*"([^"]+)"\s*,\s*(-?\d+)')
ROW = re.compile(r'\{"([^"]+)",\s*(-?\d+),')
# Synthesised by the builder rather than taken from the resource, and the
# builder is what guarantees there is exactly one of each.
SKIP = {"IDOK", "IDCANCEL"}


def main():
    with open("qtrcdlg.h", newline="") as f:
        table = f.read()
    with open("qtdialog.cpp", newline="") as f:
        src = f.read()

    dialogs = {}
    for name, body in ARRAY.findall(table):
        counts = collections.Counter()
        for sid, idx in ENTRY.findall(body):
            counts[(sid, int(idx))] += 1
        dialogs[name[5:]] = counts

    bounds = [(m.start(), m.group(1)) for m in FUNC.finditer(src)]
    bounds.append((len(src), None))

    checked = unresolved = 0
    shared = collections.defaultdict(set)
    for i in range(len(bounds) - 1):
        start, fname = bounds[i]
        body = src[start:bounds[i + 1][0]]
        used = {d for d in USES.findall(body) if d in dialogs}
        if len(used) != 1:
            continue                      # a helper, or builds nothing
        dlg = used.pop()
        controls = dialogs[dlg]

        wanted = {(s, -1) for s in BARE.findall(body)}
        wanted |= {(s, int(n)) for s, n in IDXD.findall(body)}
        wanted |= {(s, int(n)) for s, n in ROW.findall(body)}
        for sid, idx in sorted(wanted):
            if sid in SKIP:
                continue
            n = controls.get((sid, idx), 0)
            checked += 1
            if n != 1:
                unresolved += 1
                print("%-11s %-22s %-14s nIdx=%-4d matches %d control(s)"
                      % ("UNRESOLVED", fname, sid, idx, n))
            if idx == -1 and any(k[0] == sid and k[1] >= 0 for k in controls):
                shared[dlg].add(sid)

    print("%d lookup(s) checked against their own dialog, %d that do not "
          "resolve to exactly one control" % (checked, unresolved))
    if shared:
        total = sum(len(v) for v in shared.values())
        print("%d symbol(s) where a bare name sits beside indexed controls in "
              "the same dialog, and the index is what tells them apart:"
              % total)
        for dlg in sorted(shared):
            print("  %-12s %s" % (dlg, " ".join(sorted(shared[dlg]))))
    return 1 if unresolved else 0


sys.exit(main())
