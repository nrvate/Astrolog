#!/usr/bin/env python3
"""Check that each dialog control drives the same setting in both builds.

    python3 tools/rc_field_audit.py

tools/rc_audit.py finds controls that nothing wires up at all. This finds
the quieter fault: a control wired to the *wrong* variable, which every
other check here passes happily. That is not hypothetical -- Windows'
Graphics Settings writes gs.fLabelAsp for its Atlas City Coloring box,
which is the aspect-glyph setting and almost certainly an upstream typo,
and it was caught by reading rather than by any tool.

Both sides name controls the same way, but reach them differently:

  wdialog.cpp   SetCheck(dxSe_sr, us.fEquator)
                us.fEquator = GetCheck(dxSe_sr)

  qtdialog.cpp  {"dxSe_sr", -1, &us.fEquator, fFalse}      table, bare id
                {"dxSe_sr",  0, &us.fEquator2, fFalse}     table, id + "0"
                pcb = PwRcFindQt(rgbuilt, "dxMo_Ym")       by hand
                pcb = PwRcFindIdxQt(rgbuilt, "dxMo_", 80)  base + index
                pcb->setChecked(us.fMoonMove != 0)

The index forms are the trap: "dxMo_" with 80 is the control dxMo_80, and
a table row with index -1 is the bare name. Reading them as separate
things produces confident nonsense -- an early version of this script
reported four mismatches, and all four were itself.

POLARITY IS PART OF THE WIRING, and this used to ignore it. Four
controls are inverted on Windows -- "Don't Draw Background", "Skip
PostScript Header", "Expressions Off" and one eclipse box -- written as
SetCheck(ctl, !gs.fBackDraw), and the old regex required the argument to
begin with "us." or "gs.", so a leading "!" made the binding invisible
here rather than wrong. Those four had no field check at all. The Qt
side spells the same thing two ways: the RCFLAG table's fourth column
(fTrue means invert -- RcLoadFlagsQt XORs it), or a "!" in a hand
written setChecked(). Both are read now, and a box that shows the
opposite of the truth is a mismatch like any other. All four were
already right; what was missing was anything that would have said so.
"""
import re
import sys


def windows_map(path="wdialog.cpp"):
    with open(path, newline='') as f:
        s = f.read()
    out = {}
    for m in re.finditer(
            r'SetCheck\(\s*(\w+)\s*,\s*(!?)\s*((?:us|gs)\.\w+)', s):
        out.setdefault(m.group(1), set()).add((m.group(3), bool(m.group(2))))
    for m in re.finditer(
            r'((?:us|gs)\.\w+)\s*=\s*(!?)\s*GetCheck\(\s*(\w+)\s*\)', s):
        out.setdefault(m.group(3), set()).add((m.group(1), bool(m.group(2))))
    return out


def qt_map(path="qtdialog.cpp"):
    with open(path, newline='') as f:
        s = f.read()
    out = {}

    def add(ctl, fld, fInv):
        out.setdefault(ctl, set()).add((fld, fInv))

    # Table rows: {"base", index, &field, fInvert}. The fourth column is
    # what RcLoadFlagsQt() XORs the flag with, so it is the polarity.
    for m in re.finditer(
            r'\{\s*"(\w+)"\s*,\s*(-?\d+)\s*,\s*&((?:us|gs)\.\w+)'
            r'\s*,\s*(fTrue|fFalse)', s):
        base, idx, fld = m.group(1), int(m.group(2)), m.group(3)
        add(base if idx < 0 else "%s%d" % (base, idx), fld,
            m.group(4) == "fTrue")

    # Hand-wired: a variable bound to a control, then used with a field.
    var = {}
    for m in re.finditer(r'(\w+)\s*=\s*\([^)]*\)\s*PwRcFindQt\([^,]*,\s*"(\w+)"', s):
        var[m.group(1)] = m.group(2)
    for m in re.finditer(
            r'(\w+)\s*=\s*\([^)]*\)\s*PwRcFindIdxQt\([^,]*,\s*"(\w+)"\s*,\s*(\d+)', s):
        var[m.group(1)] = "%s%s" % (m.group(2), m.group(3))
    for name, ctl in var.items():
        for m in re.finditer(
                re.escape(name) + r'->setChecked\(\s*(!?)\s*((?:us|gs)\.\w+)',
                s):
            add(ctl, m.group(2), bool(m.group(1)))
        for m in re.finditer(
                r'((?:us|gs)\.\w+)\s*=\s*(!?)\s*' + re.escape(name) +
                r'->isChecked', s):
            add(ctl, m.group(1), bool(m.group(2)))
    return out


def main():
    win, qt = windows_map(), qt_map()
    both = sorted(set(win) & set(qt))
    bad = [(c, sorted(win[c]), sorted(qt[c])) for c in both if win[c] != qt[c]]

    def show(pairs):
        return ",".join(("!" if inv else "") + fld for fld, inv in pairs)

    for c, a, b in bad:
        print("MISMATCH %-12s windows=%s qt=%s" % (c, show(a), show(b)))
    print("%d control(s) checked in both builds, %d wired to a different "
          "setting or the opposite polarity" % (len(both), len(bad)))
    only = sorted(set(win) - set(qt))
    if only:
        print("%d bound on Windows and not found here (radio groups and "
              "dialogs this port does not implement): %s"
              % (len(only), " ".join(only)))
    return 1 if bad else 0


sys.exit(main())
