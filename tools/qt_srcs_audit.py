#!/usr/bin/env python3
"""Check tools/qt-srcs.py against the makefiles it stands in for.

The MSVC build in .github/workflows/windows-qt.yml cannot use make: it
compiles the Qt port with cl.exe over a response file. tools/qt-srcs.py
produces that list, and reads Makefile.srcs so it does not become a
second copy of it.

Reading Makefile.srcs protects against one drift and not the other. If a
group is renamed or deleted, qt-srcs.py stops with "cannot find SRC_X"
and the build fails loudly. But if a makefile starts using a NEW group,
qt-srcs.py's own list of group names is short by one, and it emits a list
that is quietly missing those sources -- which surfaces as a link error
on a Windows runner, attributed to nothing.

So: the groups the Qt makefiles reference must be exactly the groups
qt-srcs.py asks for. Seconds to run, and it fails on a laptop instead of
on a runner.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# (makefile, qt-srcs.py argv) pairs. SRC_WIN is deliberately absent from
# both sides: the Qt build is Windows-without-the-Windows-backend.
PAIRS = [('Makefile.qt', []), ('Makefile.qt.test', ['--test'])]


def groups_used(makefile):
    text = open(os.path.join(ROOT, makefile)).read()
    return set(re.findall(r'\$\((SRC_[A-Z_]+)\)', text))


def groups_asked(argv):
    text = open(os.path.join(ROOT, 'tools', 'qt-srcs.py')).read()
    m = re.search(r"names\s*=\s*\[([^\]]*)\]", text)
    if m is None:
        sys.stderr.write("cannot find the group list in qt-srcs.py\n")
        sys.exit(1)
    names = set(re.findall(r"'(SRC_[A-Z_]+)'", m.group(1)))
    if '--test' in argv:
        for extra in re.findall(r"names\.append\('(SRC_[A-Z_]+)'\)", text):
            names.add(extra)
    return names


# Headers that do not exist on Windows. An include of one of these at
# the top level of a file the Qt build compiles is a Windows build break
# waiting for a Windows runner to find it -- which is exactly how qttest.cpp's
# <unistd.h> was found, after four sanitizer sweeps and a warning ledger
# across five builds had all been silent about it, because on Linux it is
# correct. Inside any #if/#ifdef it is somebody's deliberate branch:
# io.cpp guards <dirent.h> behind #ifdef PC, sweph.cpp guards <dlfcn.h>
# behind #ifdef __GNUC__, and both are fine.
POSIX_ONLY = ('unistd.h', 'dirent.h', 'pwd.h', 'termios.h', 'poll.h',
              'dlfcn.h', 'sys/wait.h', 'sys/mman.h', 'sys/ioctl.h',
              'sys/socket.h', 'netinet/in.h', 'arpa/inet.h')


def unguarded_posix(sources):
    """POSIX-only includes at preprocessor depth 0, per source file."""
    hits = []
    for name in sources:
        path = os.path.join(ROOT, name)
        if not os.path.exists(path):
            continue
        depth = 0
        for i, line in enumerate(open(path, errors='replace'), 1):
            t = line.lstrip()
            if re.match(r'#\s*(if|ifdef|ifndef)\b', t):
                depth += 1
            elif re.match(r'#\s*endif\b', t):
                depth = max(0, depth - 1)
            elif depth == 0:
                m = re.match(r'#\s*include\s*<([^>]+)>', t)
                if m and m.group(1) in POSIX_ONLY:
                    hits.append((name, i, m.group(1)))
    return hits


def main():
    bad = 0
    srcs = os.popen("python3 '%s' --test"
                    % os.path.join(ROOT, 'tools', 'qt-srcs.py')).read().split()
    hits = unguarded_posix([x for x in srcs if x.endswith('.cpp')])
    if hits:
        bad = 1
        print('POSIX-only headers included unconditionally:')
        for name, line, hdr in hits:
            print('  %s:%d  <%s>' % (name, line, hdr))
        print('\nThese files are compiled by the MSVC Qt build, where the\n'
              'header does not exist. Guard it, or use the Qt equivalent --\n'
              'QDir::tempPath(), QCoreApplication::applicationPid(),\n'
              'QFile::remove() replaced the last set.')
    groups_bad = 0
    for makefile, argv in PAIRS:
        used, asked = groups_used(makefile), groups_asked(argv)
        label = 'qt-srcs.py %s' % (' '.join(argv) or '(no args)')
        if used != asked:
            bad = groups_bad = 1
            print('%s does not match %s:' % (label, makefile))
            for g in sorted(used - asked):
                print('  %s is compiled by %s and NOT in qt-srcs.py'
                      % (g, makefile))
            for g in sorted(asked - used):
                print('  %s is in qt-srcs.py and NOT compiled by %s'
                      % (g, makefile))
    if groups_bad:
        print('\nThe MSVC build would compile a different set of sources '
              'than\nmake does. Fix the group list in tools/qt-srcs.py.')
    if bad:
        return 1
    print('qt-srcs audit clean: %d makefiles agree, %d sources free of '
          'unguarded POSIX headers' % (len(PAIRS), len(srcs)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
