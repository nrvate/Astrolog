#!/usr/bin/env python3
"""Check that every control lookup is cast to the type the resource builds.

    python3 tools/rc_casttype_audit.py

RcBuildDialogQt() turns each RCCTL row into one widget, and the row's
ctl* kind decides which: ctlEdit is a QLineEdit, ctlCombo a QComboBox,
ctlCheck a QCheckBox, and so on. Every dialog then reaches back for its
controls by name and casts the QWidget * it gets to the type it expects:

    QComboBox *pcb = (QComboBox *)PwRcFindQt(rgbuilt, "dcCa_e");

That cast is a C style cast on a QWidget *, so it is not checked at
compile time and not checked at run time either. If the control in
astrolog.rc is an EDITTEXT rather than a COMBOBOX, the pointer is real,
non-NULL, and of the wrong class -- and the next line calls
currentText() on a QLineEdit. Undefined behaviour, and the shape of it
is a crash somewhere else entirely, or silent nonsense.

Nothing else here can see that. rc_lookup_audit checks a lookup resolves
to exactly one control; rc_field_audit that it is wired to the right
setting; rc_flagtype_audit that a flag-bound id is a checkbox. None of
them looks at the C++ type on the left of the assignment.

Scope is per dialog, for the reason rc_lookup_audit.py gives at length:
qtrcdlg.h holds every dialog concatenated and the same ids recur, so a
table-wide check passes on nearly anything. Each cast is resolved
against the controls of the dialog whose array its function builds.

Casts to QWidget * and to QAbstractButton * are legal for any control
and are not reported.
"""

import collections
import re
import sys

ARRAY = re.compile(r'static CONST RCCTL (rgctl\w+)\[\] = \{(.*?)\n\};', re.S)
ENTRY = re.compile(
    r'\{(ctl\w+),\s*(?:"(?:[^"\\]|\\.)*"|NULL),\s*"([^"]+)",\s*(-?\d+)')
# A function definition at column 0, through to the next one.
FUNC = re.compile(r'^(?:[A-Za-z_][\w *]*?)\b(\w+Qt)\s*\([^;]*?\)\s*\n\{', re.M)
USES = re.compile(r'\brgctl(\w+)\b')

# (QType *)PwRcFindQt(rg, "id") and the indexed form, plus the
# qobject_cast spelling of each. The lookup helpers return QWidget *, so
# every use worth checking has a type written beside it.
CCAST = re.compile(
    r'\(\s*(Q\w+)\s*\*\s*\)\s*PwRcFind(Idx)?Qt\('
    r'\s*\w+\s*,\s*"([^"]+)"(?:\s*,\s*(-?\d+))?')
QCAST = re.compile(
    r'qobject_cast\s*<\s*(Q\w+)\s*\*\s*>\s*\(\s*PwRcFind(Idx)?Qt\('
    r'\s*\w+\s*,\s*"([^"]+)"(?:\s*,\s*(-?\d+))?')

# What RcBuildDialogQt() constructs for each kind. A kind absent from
# here builds no widget at all -- the dialog-class marker rows, which no
# lookup can name because PwRcFindQt() only ever returns a built one.
BUILDS = {
    "ctlGroup":  "QGroupBox",
    "ctlCheck":  "QCheckBox",
    "ctlRadio":  "QRadioButton",
    "ctlLabel":  "QLabel",
    "ctlButton": "QPushButton",
    "ctlEdit":   "QLineEdit",
    "ctlCombo":  "QComboBox",
    "ctlList":   "QListWidget",
    "ctlIcon":   "QLabel",
}

# Bases every widget above can be reached through, so casting to one is
# correct by construction and says nothing about the control's kind.
BASES = {"QWidget", "QObject", "QFrame"}
# QCheckBox, QRadioButton and QPushButton are all QAbstractButtons.
BUTTONS = {"QCheckBox", "QRadioButton", "QPushButton"}


def main():
    with open("qtrcdlg.h", newline="") as f:
        table = f.read()
    with open("qtdialog.cpp", newline="") as f:
        src = f.read()

    dialogs = collections.defaultdict(dict)
    for name, body in ARRAY.findall(table):
        for kind, sid, idx in ENTRY.findall(body):
            dialogs[name[5:]][(sid, int(idx))] = kind
    if not dialogs:
        print("no dialogs parsed out of qtrcdlg.h -- has its shape changed? "
              "This audit reads that table directly.")
        return 1

    bounds = [(m.start(), m.group(1)) for m in FUNC.finditer(src)]
    bounds.append((len(src), None))

    checked = 0
    bad = []
    for i in range(len(bounds) - 1):
        start, fname = bounds[i]
        body = src[start:bounds[i + 1][0]]
        used = {d for d in USES.findall(body) if d in dialogs}
        if len(used) != 1:
            continue                      # a helper, or builds nothing
        controls = dialogs[used.pop()]

        for rx in (CCAST, QCAST):
            for typ, fIdx, sid, idx in rx.findall(body):
                idx = int(idx) if fIdx and idx != "" else -1
                kind = controls.get((sid, idx))
                if kind is None:
                    continue              # rc_lookup_audit's business
                checked += 1
                if typ in BASES:
                    continue
                want = BUILDS.get(kind)
                if want is None:
                    bad.append((fname, sid, idx, typ, kind, "builds nothing"))
                elif typ == "QAbstractButton" and want in BUTTONS:
                    continue
                elif typ != want:
                    bad.append((fname, sid, idx, typ, kind, want))

    if bad:
        for fname, sid, idx, typ, kind, want in bad:
            print("  %-34s %-16s nIdx=%-3d cast to %-14s but %s builds a %s"
                  % (fname, sid, idx, typ, kind, want))
        print("\nThese are C style casts on a QWidget *, so neither the")
        print("compiler nor Qt catches them: the call that follows lands on")
        print("an object of the wrong class. Either fix the type, or fix the")
        print("control in astrolog.rc and regenerate qtrcdlg.h.")
        return 1

    print("rc cast types clean: %d lookup(s) cast to the type their own "
          "dialog builds" % checked)
    return 0


sys.exit(main())
