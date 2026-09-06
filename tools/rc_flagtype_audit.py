#!/usr/bin/env python3
"""Every RCFLAG row names a control that really is a checkbox.

qtdialog.cpp binds dialog checkboxes to program flags through a table:

    {"dxGr_Xm", -1, &gs.fColor, fFalse},

RcLoadFlagsQt()/RcStoreFlagsQt() look each id up and call setChecked() /
isChecked() on the result. The id is DATA -- nothing beside it says what
kind of control it names -- and the lookup used to be a C-style cast, so
a row pointing at an edit box or a label reached those methods through
the wrong type, which is undefined behaviour rather than a wrong answer.
It is a qobject_cast since 2026-09-06, which turns that into the control
silently not working instead. Neither is what anyone wants, and neither
is loud.

No existing audit covers it. rc_lookup_audit.py checks that an id
resolves to exactly one control; rc_field_audit.py checks that a control
is wired to the right SETTING. Neither checks that the control is the
KIND the code then treats it as.

Exit 0 when every row names a ctlCheck or ctlRadio, 1 otherwise.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    dlg = open(os.path.join(ROOT, 'qtdialog.cpp')).read()
    rc = open(os.path.join(ROOT, 'qtrcdlg.h')).read()

    # What kind of control each id is, from the generated resource table.
    # An id can appear in several dialogs; collect every kind it is used as.
    kinds = {}
    for m in re.finditer(r'\{(ctl\w+),\s*"(?:[^"\\]|\\.)*",\s*"([^"]+)"', rc):
        kinds.setdefault(m.group(2), set()).add(m.group(1))
    if not kinds:
        print('no controls parsed out of qtrcdlg.h -- has its shape changed? '
              'This audit reads that table directly.')
        return 1

    # An RCFLAG row is {"id", <index>, &flag, <invert>}. The "&" is what
    # separates these from every other braced table in the file.
    rows = sorted(set(re.findall(
        r'\{\s*"([A-Za-z0-9_]+)"\s*,\s*-?\d+\s*,\s*&', dlg)))
    if not rows:
        print('no RCFLAG rows found in qtdialog.cpp -- has the table shape '
              'changed? This audit reads it as text.')
        return 1

    missing = [r for r in rows if r not in kinds]
    wrong = [(r, sorted(kinds[r])) for r in rows
             if r in kinds and not (kinds[r] & {'ctlCheck', 'ctlRadio'})]

    if missing or wrong:
        for r in missing:
            print('  %s: bound to a flag, but no such control in qtrcdlg.h'
                  % r)
        for r, k in wrong:
            print('  %s: bound to a flag, but it is %s, not a checkbox'
                  % (r, '/'.join(k)))
        print('\nRcLoadFlagsQt()/RcStoreFlagsQt() call setChecked() and')
        print('isChecked() on whatever these name. Either bind the row to')
        print('the checkbox it meant, or handle the control where it is')
        print('built rather than through the flag table.')
        return 1

    print('rc flag types clean: %d flag-bound ids, all checkboxes' % len(rows))
    return 0


if __name__ == '__main__':
    sys.exit(main())
