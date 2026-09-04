#!/usr/bin/env python3
"""The fork's version, in whichever spelling a tool wants.

    python3 tools/version.py           # 8.00-qt.5      what astrolog.h says
    python3 tools/version.py --quad    # 8.0.0.5        four dotted numbers
    python3 tools/version.py --rc      # 8,0,0,5        the same, for a
                                       #                VERSIONINFO resource

tools/ci-assert-version.sh is the sh spelling of the first form and the
installer script derives the second with sed. This exists for the MSVC
build, where there is no sh: the Windows resource compiler wants the
version as four comma-separated numbers on the command line, and the
.cmd that drives it cannot run the shell scripts.

The mapping is the one tools/package-windows-installer.sh uses, so the
version Explorer shows for astrolog.exe and the version it shows for
the installer are derived the same way: 8.00-qt.5 -> 8.0.0.5.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def version():
    """szVersionCore "-qt." szVersionFork, the way astrolog.h composes
    szVersionQt and tools/ci-assert-version.sh composes the tag."""
    text = open(os.path.join(ROOT, "astrolog.h")).read()
    parts = {}
    for name in ("szVersionCore", "szVersionFork"):
        m = re.search(r'^#define %s\s+"([^"]*)"' % name, text, re.M)
        if not m:
            sys.stderr.write("cannot read %s from astrolog.h\n" % name)
            sys.exit(1)
        parts[name] = m.group(1)
    return "%s-qt.%s" % (parts["szVersionCore"], parts["szVersionFork"])


def quad(ver):
    m = re.match(r"^(\d+)\.(\d+)-qt\.(\d+)$", ver)
    if not m:
        sys.stderr.write("cannot derive a numeric version from '%s' (want N.N-qt.N)\n" % ver)
        sys.exit(1)
    return [int(m.group(1)), int(m.group(2)), 0, int(m.group(3))]


def main(argv):
    ver = version()
    if "--quad" in argv:
        print(".".join(str(n) for n in quad(ver)))
    elif "--rc" in argv:
        print(",".join(str(n) for n in quad(ver)))
    else:
        print(ver)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
