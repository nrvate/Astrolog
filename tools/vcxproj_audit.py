#!/usr/bin/env python3
"""Astrolog.vcxproj lists exactly the sources Makefile.win compiles.

    python3 tools/vcxproj_audit.py

The MSVC project is a second Windows build definition, and a second build
definition is the thing swisseph's workflow comments warn about three
times: the unbuilt one rots. This one did. It went from a78436f, which
added switch.cpp, to 2026-09-02 without it -- so anybody opening the
project in Visual Studio got a link error, and nothing in the tree said
why.

CI builds it now, which stops it rotting into "does not compile". This
audit is the cheaper half of the same guarantee: a source added to
Makefile.srcs and not to the project fails here, on Linux, in a second,
rather than on a Windows runner in a minute. Same reason the generated
tables are diffed rather than trusted.

Exit 0 when the two lists are equal, 1 otherwise.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    mk = open(os.path.join(ROOT, 'Makefile.srcs')).read()
    srcs = set()
    # The Windows build is SRC_CORE + SRC_SWISS + SRC_WIN + SRC_GRAPHICS;
    # see Makefile.win. SRC_QT and SRC_TEST are deliberately not in it.
    for var in ('SRC_CORE', 'SRC_SWISS', 'SRC_WIN', 'SRC_GRAPHICS'):
        m = re.search(var + r'\s*=\s*((?:[^\n]*\\\n)*[^\n]*)', mk)
        if m is None:
            print('cannot find %s in Makefile.srcs -- has it been renamed?'
                  % var)
            return 1
        srcs |= set(re.findall(r'([A-Za-z0-9_]+\.cpp)', m.group(1)))

    proj_path = os.path.join(ROOT, 'Astrolog.vcxproj')
    # newline='' so the file's CRLF is not translated: .gitattributes keeps
    # this one CRLF for Visual Studio, and reading it in text mode is how
    # a well-meaning rewrite converts it.
    with open(proj_path, newline='') as f:
        proj = set(re.findall(r'ClCompile Include="([^"]+)"', f.read()))

    missing = sorted(srcs - proj)
    extra = sorted(proj - srcs)
    if missing or extra:
        if missing:
            print('in Makefile.win but not in Astrolog.vcxproj (%d):' %
                  len(missing))
            for s in missing:
                print('  %s' % s)
        if extra:
            print('in Astrolog.vcxproj but not in Makefile.win (%d):' %
                  len(extra))
            for s in extra:
                print('  %s' % s)
        print('\nAdd or remove the <ClCompile Include="..."/> line. The file'
              ' is CRLF;')
        print('edit it in a way that keeps that, or tools/line_endings_audit.py'
              ' will')
        print('not catch the conversion -- it skips .vcxproj by design.')
        return 1

    print('vcxproj audit clean: %d sources, same set as Makefile.win'
          % len(srcs))
    return 0


if __name__ == '__main__':
    sys.exit(main())
