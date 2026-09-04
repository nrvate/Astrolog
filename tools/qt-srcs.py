#!/usr/bin/env python3
"""Print the source files a Qt build compiles, one per line.

    python3 tools/qt-srcs.py            # the Qt application
    python3 tools/qt-srcs.py --test     # and the in-process suite
    python3 tools/qt-srcs.py --only core   # SRC_CORE + SRC_GRAPHICS + SRC_SWISS:
                                           # what the two binaries SHARE
    python3 tools/qt-srcs.py --only qt     # SRC_QT: compiled per binary,
    python3 tools/qt-srcs.py --only test   # SRC_TEST: the suite's own file

Makefile.srcs is the single source list for every build here, and the
makefiles read it directly. A build that cannot use make -- the Windows
package, which tools/msvc-build-qt.cmd compiles with cl.exe for
.github/workflows/windows-qt.yml, since the port has no Q_OBJECT and
therefore no moc step and therefore no need for a build system -- still
must not keep its own copy of the list, or it becomes the sixth place a
new source file has to be remembered.

The Qt build is SRC_CORE + SRC_GRAPHICS + SRC_SWISS + SRC_QT, which is
what Makefile.qt uses. SRC_WIN is deliberately absent.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def group(text, name):
    m = re.search(name + r'\s*=\s*((?:[^\n]*\\\n)*[^\n]*)', text)
    if m is None:
        sys.stderr.write('cannot find %s in Makefile.srcs\n' % name)
        sys.exit(1)
    return re.findall(r'([A-Za-z0-9_]+\.cpp)', m.group(1))


def main():
    text = open(os.path.join(ROOT, 'Makefile.srcs')).read()
    names = ['SRC_CORE', 'SRC_GRAPHICS', 'SRC_SWISS', 'SRC_QT']
    if '--test' in sys.argv:
        names.append('SRC_TEST')
    if '--only' in sys.argv:
        # The split the MSVC build compiles by: the shared core once, the
        # Qt sources once per binary (they differ under -DQTTEST), the
        # suite's own file once. Everything a build here compiles is still
        # one of these three, so nothing is listed a second time.
        which = sys.argv[sys.argv.index('--only') + 1]
        names = {'core': ['SRC_CORE', 'SRC_GRAPHICS', 'SRC_SWISS'],
                 'qt': ['SRC_QT'], 'test': ['SRC_TEST']}.get(which)
        if names is None:
            sys.stderr.write('--only wants core, qt or test\n')
            return 2
    out = []
    for n in names:
        for f in group(text, n):
            if f not in out:
                out.append(f)
    if not out:
        sys.stderr.write('empty source list -- refusing\n')
        return 1
    print('\n'.join(out))
    return 0


if __name__ == '__main__':
    sys.exit(main())
