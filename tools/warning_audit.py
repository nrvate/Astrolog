#!/usr/bin/env python3
"""Compile every build with warnings turned all the way up, and hold the
result against a committed baseline.

Nothing in this project has ever read a compiler warning. The build checks
match ": error:" and "^make.*\\*\\*\\*" on purpose (CLAUDE.md says why: a
check matching " error " does not match "Error 1", and every test after it
runs against the stale binary). Warnings fell through that gap entirely,
and the gap has a measured cost -- see the header of tools/warnings.txt.

What this does:

  1. Builds all four targets clean, each with its real flags plus -Wall.
     The flag sets here are supersets of the makefiles' own, so this can
     only see more than an ordinary build, never less.
  2. Normalizes every warning to (build, file, function, flag, message
     with all numbers masked) and counts the duplicates.
  3. Diffs that against tools/warnings.txt and fails on anything new.

Numbers are masked because a legitimate edit moves them -- "a region of
size 80" becomes "size 96" when a buffer grows -- while the *shape* of the
warning is what identifies the site. Line numbers are dropped for the same
reason, one order of magnitude worse: every insertion above a warning
would otherwise rewrite the baseline. The function name is the anchor
instead, which survives edits and still says where to look.

  tools/warning_audit.py                 # all four builds, ~6 minutes
  tools/warning_audit.py --build console # just one
  tools/warning_audit.py --update        # rewrite the baseline
  tools/warning_audit.py --file io.cpp   # one file, seconds, no baseline

Exit 0 when the report matches the baseline, 1 otherwise.

The Windows build is here for one reason worth stating: Makefile.win
compiled with -w until 2026-09-01, so wdriver.cpp and wdialog.cpp -- which
no other build compiles at all -- had never been diagnosed by anything.
Turning warnings on there costs nothing (mingw g++ 10 reports zero at
default level) and it is the only net those two files have.

But it is NOT the net for the format-truncation class. mingw redirects
snprintf to __mingw_snprintf, which GCC does not recognize as the builtin,
so its format analysis is silently absent there. Measured, not assumed:
the same tree reported 44 format-truncation warnings under g++ 11 on
Linux
and 0 under mingw g++ 10.
"""

import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE = os.path.join(ROOT, 'tools', 'warnings.txt')

# -j4 and no higher: the maintainer's machine shares this NAS with other
# work, and a wider build has been asked not to happen.
JOBS = '-j4'

# Every build's real flags plus -Wall. Keep these in step with the
# makefiles when those change; the audit deliberately owns its own copy so
# that turning a warning class on here does not disturb an ordinary build.
COMMON_NO = '-Wno-write-strings -Wno-narrowing -Wno-comment'

BUILDS = {
    # name:      (makefile,          flag variable, flags)
    'console':   ('Makefile',        'CPPFLAGS',
                  '-O -Wall ' + COMMON_NO),
    'qt':        ('Makefile.qt',     'CPPFLAGS',
                  '-DQT -O -fPIC -Wall ' + COMMON_NO + ' $(QT_CFLAGS)'),
    'qt-test':   ('Makefile.qt.test', 'CPPFLAGS',
                  '-DQT -DQTTEST -O -fPIC -Wall ' + COMMON_NO +
                  ' $(QT_CFLAGS)'),
    # -std=gnu++17 is not decoration: mingw g++ 10 defaults to gnu++14,
    # where calc.cpp's "Borrow bciCore(ciCore);" (class template argument
    # deduction, C++17) is an error. -w was hiding that too -- the build
    # only ever compiled because -fpermissive downgraded it and -w then
    # swallowed the message.
    'win':       ('Makefile.win',    'CFLAGS',
                  '-DWIN -DPC -D_WINDOWS -DWIN32 -D__CRT__NO_INLINE -O2 '
                  '-fpermissive -static -std=gnu++17 -Wall ' + COMMON_NO),
    # The console Windows build. Same toolchain and nearly the same core
    # as 'win', but -DWCLI compiles xscreen.cpp's WCLI block that -DWIN
    # does not, so it is not covered by that entry: measured at 82 -Wall
    # warnings against win's 89, overlapping heavily and not identically.
    # It was outside every net here from the day Makefile.wcli was added
    # until this entry, which is the same "compiled by something, read by
    # nothing" gap the Qt6 build had.
    'wcli':      ('Makefile.wcli',   'CFLAGS',
                  '-DWCLI -DPC -D_WINDOWS -DWIN32 -D__CRT__NO_INLINE -O2 '
                  '-fpermissive -static -std=gnu++17 -Wall ' + COMMON_NO),
}

# The Qt6 build is deliberately NOT in BUILDS, and the reason is the
# ledger's first column: it names the set of builds that agree on a site,
# so adding a fifth would rewrite nearly every line ("console+qt+qt-test"
# becomes "console+qt+qt-test+qt6") -- and rewrite it back on any machine
# without Qt6, which is most of them. It gets its own small ledger
# instead, checked only where a Qt6 exists to check it against.
#
# It earned one: the Qt6 build named two deprecated calls on every compile
# and nothing was reading them, because this audit covered the other four.
QT6_PKGCONFIG = os.environ.get('QT6_PKGCONFIG', '/usr/local/qt6/lib/pkgconfig')
QT6_BASELINE = os.path.join(ROOT, 'tools', 'warnings-qt6.txt')
QT6_BUILD = ('Makefile.qt', 'CPPFLAGS',
             '-DQT -O -fPIC -Wall ' + COMMON_NO + ' $(QT_CFLAGS)')
QT6_ARGS = ['OBJDIR=obj-qt6', 'NAME=astrolog-qt6']
# The Qt6 build of the suite. CI compiles it ("make qt6-test") and runs
# 3792 assertions against it, but nothing read its warnings until this
# entry -- and qttest.cpp is the largest and most actively edited Qt
# consumer in the tree, so a Qt6 deprecation there is exactly what this
# ledger exists to show.
QT6_TEST_BUILD = ('Makefile.qt.test', 'CPPFLAGS',
                  '-DQT -DQTTEST -O -fPIC -Wall ' + COMMON_NO +
                  ' $(QT_CFLAGS)')
QT6_TEST_ARGS = ['OBJDIR=obj-qt6-test', 'NAME=astrolog-qt6-test']


def qt6_env():
    """The environment the Qt6 build needs, or None when there is no Qt6.

    Absence is not a failure: most machines have no Qt6, and this audit
    has to stay a gate on those. It says which it did.
    """
    env = dict(os.environ, PKG_CONFIG_PATH=QT6_PKGCONFIG)
    p = subprocess.run(['pkg-config', '--exists', 'Qt6Widgets'], env=env)
    return env if p.returncode == 0 else None


# GCC quotes identifiers with U+2018/U+2019.
RE_WARN = re.compile(
    r'^(?P<file>[\w./+-]+\.(?:cpp|h)):(?P<line>\d+):(?P<col>\d+): '
    r'warning: (?P<msg>.*?) \[(?P<flag>-W[\w=+-]*)\]$')
RE_CONTEXT = re.compile(
    r'^(?P<file>[\w./+-]+\.(?:cpp|h)): In (?:function|member function|'
    r'constructor|destructor|lambda function|instantiation of) '
    r'[‘\'](?P<func>.*)[’\']:$')
RE_SCOPE = re.compile(r'^(?P<file>[\w./+-]+\.(?:cpp|h)): At global scope:$')
RE_ERROR = re.compile(r': error: |^make.*\*\*\*')

# A function's signature is noise for identification and churns on every
# prototype change -- and 20 of those moved in work log item 145 alone.
# Keep the bare name.
RE_FUNCNAME = re.compile(r'([A-Za-z_]\w*)\s*\(')


def short_func(sig):
    """'flag FLoadAtlas(FILE*, int)' -> 'FLoadAtlas'."""
    m = RE_FUNCNAME.search(sig)
    return m.group(1) if m else sig


def mask(msg):
    """Numbers move when a buffer is resized; the shape does not."""
    return re.sub(r'\d+', 'N', msg)


def run_build(name, clean=True):
    """Compile one target from scratch and return its raw output.

    Returns (output, failed). A build that fails to compile is not a
    warning question -- it is reported and stops the audit, because every
    warning after the failure is missing and the report would look clean.
    """
    if name == 'qt6':
        makefile, var, flags = QT6_BUILD
        extra, env = QT6_ARGS, qt6_env()
    elif name == 'qt6-test':
        makefile, var, flags = QT6_TEST_BUILD
        extra, env = QT6_TEST_ARGS, qt6_env()
    else:
        makefile, var, flags = BUILDS[name]
        extra, env = [], None
    if clean:
        subprocess.run(['make', '-f', makefile, 'clean'] + extra, cwd=ROOT,
                       env=env, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
    p = subprocess.run(['make', '-f', makefile, JOBS, '%s=%s' % (var, flags)]
                       + extra, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True, errors='replace')
    failed = p.returncode != 0 or bool(RE_ERROR.search(p.stdout))
    return p.stdout, failed


def parse(output, build):
    """Fold one build's output into {(build, file, func, flag, msg): count}.

    Attribution is per file rather than "whatever context line came last",
    because -j4 interleaves four compilers into one stream. Each file is
    compiled by exactly one process, so its own lines stay in order.
    """
    counts = {}
    context = {}
    for line in output.splitlines():
        m = RE_CONTEXT.match(line)
        if m:
            context[m.group('file')] = short_func(m.group('func'))
            continue
        m = RE_SCOPE.match(line)
        if m:
            context[m.group('file')] = '(global scope)'
            continue
        m = RE_WARN.match(line)
        if not m:
            continue
        f = m.group('file')
        # A warning quoted from a system header (glibc's stdio2.h notes)
        # belongs to whichever of our files pulled it in; those arrive as
        # notes rather than warnings, so anything left under /usr is ours
        # to ignore.
        if f.startswith('/'):
            continue
        # A warning inside a HEADER cannot be attributed from this stream,
        # and pretending otherwise made the audit nondeterministic. The
        # context line names the file being COMPILED; a header is compiled
        # by every .cpp that includes it, and under -j4 that is four
        # processes interleaved -- so "the last context line for
        # astrolog.h" is whichever process happened to get there first.
        # The same warning came out as SwissComputeStar here and
        # "(unknown)" on a runner, and turned a nightly red with no code
        # change behind it. Headers get one fixed label; file, flag and
        # message still identify the row.
        func = '(header)' if f.endswith('.h') else context.get(f, '(unknown)')
        key = (build, f, func, m.group('flag'),
               mask(m.group('msg')))
        counts[key] = counts.get(key, 0) + 1
    return counts


def report_lines(counts):
    """One line per site, naming every build that reports it.

    The three Linux builds compile the same 24 shared files, so recording
    them separately would triple the ledger and say nothing: a shared-core
    warning is one fact, not three. Builds that agree collapse into a
    "console+qt+qt-test" first column, and a count that differs between
    builds splits back out on its own line -- which is exactly the case
    worth seeing, since it means a warning depends on -DQT or -DQTTEST.
    """
    sites = {}
    for (build, f, func, flag, msg), n in counts.items():
        sites.setdefault((f, func, flag, msg), {})[build] = n
    out = []
    for site in sorted(sites):
        f, func, flag, msg = site
        bycount = {}
        for build, n in sites[site].items():
            bycount.setdefault(n, []).append(build)
        for n in sorted(bycount):
            builds = '+'.join(sorted(bycount[n]))
            out.append('%s\t%s\t%s\t%s\t%d\t%s' % (builds, f, func, flag,
                                                   n, msg))
    return sorted(out)


HEADER = """\
# Compiler warnings, as counted by tools/warning_audit.py.
#
# This file is a ledger of what the compiler still objects to, not a list
# of things that are fine. When one is fixed the line disappears, and the
# audit fails until this file is regenerated with --update. It fails on a
# *removed* line too, on purpose, so the ledger cannot quietly overstate
# what is left.
#
# Two counts matter and they are not the same. This audit compiles with
# -Wall so it sees more than an ordinary build ever will, which is right
# for catching regressions and wrong for knowing what the person running
# "make" has to read. That second number is ZERO as of 2026-09-01: all
# four builds compile silently (work log items 151-152, from 49 warnings
# in 722 lines of output). Everything below is -Wall only.
#
# Every class in here carries a recorded verdict, so a NEW line is a
# genuinely new warning. The verdicts, with their evidence, are in
# QT_GUI_PLAN.md's work log:
#
#   -Wmaybe-uninitialized   item 150 -- 0 definite, and 23 of them appear
#                           at one optimization level and not the other
#   -Wformat-truncation=    items 151-152 -- 45 down to 1 by sizing the
#                           destinations; the last is qtdialog.cpp and is
#                           -Wall only
#   -Wunused-result         closed in item 152 (placalc.cpp is third-party
#                           and carries a pragma with its reason)
#   -Wunused-function       item 149 -- qtdialog.cpp's documented dialog
#                           helper toolkit, kept for the next session
#   -Wunused-value          item 149 -- FreeProcInstance(), a Win16 no-op
#   -Wsign-compare          third-party (sweph.cpp)
#   -Wparentheses           third-party (placalc.cpp)
#   -Wunused-variable       item 148 -- SwissHouse()'s discarded return
#   -Wunused-but-set-var    item 148 -- CommandLineX()'s unfinished fPause
#
# Format: build, file, function, flag, count, message with numbers masked.
#
# Regenerate:  tools/warning_audit.py --update
"""


QT6_HEADER = """\
# Compiler warnings from the Qt6 build, as counted by
# tools/warning_audit.py.
#
# Separate from tools/warnings.txt on purpose. That file's first column
# names the set of builds that agree on a site, so folding a fifth build
# into it would rewrite nearly every line -- and rewrite it back on the
# many machines that have no Qt6. This one is checked only where a Qt6
# exists to check against, and skipped, not failed, everywhere else.
#
# It exists because the Qt6 build spent its whole life outside the audit:
# two deprecated QMouseEvent::globalPos() calls were named on every
# compile and nobody read them. Both now go through PtGlobalQt(), the one
# spelling Qt5 and Qt6 both accept, and this ledger is empty.
#
# An empty ledger is the goal, not an oversight. A NEW line here is a
# warning the Qt6 build has and the Qt5 build does not.
#
# Regenerate:  tools/warning_audit.py --update   (needs a Qt6)
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', choices=sorted(BUILDS), action='append',
                    help='audit only this build (repeatable)')
    ap.add_argument('--update', action='store_true',
                    help='rewrite tools/warnings.txt from this run')
    ap.add_argument('--file', metavar='SRC',
                    help='compile one source file and print its warnings; '
                         'no baseline comparison')
    ap.add_argument('--no-clean', action='store_true',
                    help='reuse existing objects (partial report; for '
                         'iterating, never for a baseline)')
    args = ap.parse_args()

    if args.file:
        return one_file(args.file)

    builds = args.build or sorted(BUILDS)
    if args.update and args.build:
        sys.stderr.write(
            'refusing to --update from a subset: the baseline covers every '
            'build, and rewriting it from one would delete the others\n')
        return 1

    counts = {}
    for name in builds:
        sys.stderr.write('building %s ...\n' % name)
        output, failed = run_build(name, clean=not args.no_clean)
        if failed:
            sys.stderr.write(
                '\n%s FAILED TO BUILD -- audit aborted.\n'
                'Every warning after the failure is missing, so a report '
                'from here would read clean.\n\n' % name)
            for line in output.splitlines():
                if RE_ERROR.search(line):
                    sys.stderr.write('  %s\n' % line)
            return 2
        counts.update(parse(output, name))

    lines = report_lines(counts)
    total = sum(counts.values())

    if args.update:
        with open(BASELINE, 'w') as f:
            f.write(HEADER)
            f.write('# Total: %d warnings in %d distinct sites.\n\n'
                    % (total, len(lines)))
            f.write('\n'.join(lines) + '\n')
        sys.stderr.write('wrote %s: %d warnings, %d sites\n'
                         % (BASELINE, total, len(lines)))
        # The Qt6 ledger is regenerated by the same --update, or this
        # would be a baseline that covers four builds out of five.
        return audit_qt6(True, counts, clean=not args.no_clean)

    if args.build:
        # Not a gate. The first column is the set of builds that agree on
        # a site, so auditing a subset renames every shared line
        # ("console+qt+qt-test" becomes "console") and a diff against the
        # full baseline would be pure noise. Print and say so.
        print('\n'.join(lines))
        sys.stderr.write(
            '\n%d warnings in %d sites across %s. Subset run: not compared '
            'against the baseline, which only the full audit can gate.\n'
            % (total, len(lines), ', '.join(builds)))
        return 0

    try:
        with open(BASELINE) as f:
            want = [l.rstrip('\n') for l in f
                    if l.strip() and not l.startswith('#')]
    except FileNotFoundError:
        sys.stderr.write('no baseline at %s; run with --update\n' % BASELINE)
        return 1

    added = [l for l in lines if l not in want]
    gone = [l for l in want if l not in lines]

    rc = 0
    if not added and not gone:
        print('warning audit clean: %d warnings in %d sites, all known'
              % (total, len(lines)))
    else:
        for l in added:
            print('NEW      %s' % l)
        for l in gone:
            print('GONE     %s' % l)
        print()
        print('%d new, %d gone. If the gone ones are fixes, regenerate:'
              % (len(added), len(gone)))
        print('  tools/warning_audit.py --update')
        rc = 1

    return audit_qt6(args.update, counts, clean=not args.no_clean) or rc

def audit_qt6(update, base_counts, clean=True):
    """The Qt6 build against its own ledger. Zero when there is no Qt6.

    Only what Qt6 warns about and Qt5 does not. The shared core produces
    the same 73 sites under either Qt, and they are already in
    tools/warnings.txt under the console/qt/qt-test builds; repeating them
    here would bury the handful of lines this ledger exists to show. What
    is left is the Qt6-specific set -- which is what the two deprecated
    globalPos() calls were, and what a Qt6 migration would have to read.
    """
    if qt6_env() is None:
        print('qt6: skipped, no Qt6 at %s' % QT6_PKGCONFIG)
        return 0
    # Both Qt6 builds, each measured against its Qt5 twin: the point is
    # what Qt6 says and Qt5 does not, so the shared core's warnings --
    # already in tools/warnings.txt -- subtract out and what remains is
    # the Qt6-specific set.
    counts = {}
    for name, twin in (('qt6', 'qt'), ('qt6-test', 'qt-test')):
        sys.stderr.write('building %s ...\n' % name)
        output, failed = run_build(name, clean=clean)
        if failed:
            sys.stderr.write('\n%s FAILED TO BUILD -- audit aborted.\n' % name)
            for line in output.splitlines():
                if RE_ERROR.search(line):
                    sys.stderr.write('  %s\n' % line)
            return 2
        for k, v in parse(output, 'qt6').items():
            if (twin,) + k[1:] not in base_counts:
                counts[k] = max(counts.get(k, 0), v)
    lines = report_lines(counts)
    total = sum(counts.values())

    if update:
        with open(QT6_BASELINE, 'w') as f:
            f.write(QT6_HEADER)
            f.write('# Total: %d warnings in %d distinct sites.\n\n'
                    % (total, len(lines)))
            if lines:
                f.write('\n'.join(lines) + '\n')
        sys.stderr.write('wrote %s: %d warnings, %d sites\n'
                         % (QT6_BASELINE, total, len(lines)))
        return 0

    try:
        with open(QT6_BASELINE) as f:
            want = [l.rstrip('\n') for l in f
                    if l.strip() and not l.startswith('#')]
    except FileNotFoundError:
        sys.stderr.write('no qt6 baseline at %s; run with --update\n'
                         % QT6_BASELINE)
        return 1

    added = [l for l in lines if l not in want]
    gone = [l for l in want if l not in lines]
    if not added and not gone:
        print('qt6 warning audit clean: %d warnings in %d sites, all known'
              % (total, len(lines)))
        return 0
    for l in added:
        print('NEW  qt6 %s' % l)
    for l in gone:
        print('GONE qt6 %s' % l)
    print('  tools/warning_audit.py --update')
    return 1


def one_file(src):
    """Compile a single file under each build's flags. Seconds, not minutes
    -- this is the loop to use while fixing, with the full audit as the
    gate at the end."""
    rc = 0
    names = sorted(BUILDS)
    if qt6_env() is not None:
        names.append('qt6')      # The fast loop covers Qt6 too, where
                                 # there is one. The full audit's qt6 leg
                                 # is minutes; this is seconds.
    for name in names:
        makefile, var, flags = (QT6_BUILD if name == 'qt6' else BUILDS[name])
        env = qt6_env() if name == 'qt6' else None
        # Ask make to expand $(QT_CFLAGS) rather than reimplementing
        # pkg-config here.
        p = subprocess.run(
            ['make', '-f', makefile, '--eval=warnaudit:;@echo %s' % flags,
             'warnaudit'], cwd=ROOT, env=env, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, text=True)
        expanded = p.stdout.strip()
        if not expanded:
            continue
        cxx = 'x86_64-w64-mingw32-g++' if name == 'win' else 'g++'
        cmd = [cxx] + expanded.split() + ['-c', '-o', '/dev/null', src]
        q = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True,
                           errors='replace')
        counts = parse(q.stdout, name)
        if RE_ERROR.search(q.stdout):
            print('%s: %s DOES NOT COMPILE' % (src, name))
            print(q.stdout)
            rc = 2
            continue
        for line in report_lines(counts):
            print(line)
    return rc


if __name__ == '__main__':
    sys.exit(main())
