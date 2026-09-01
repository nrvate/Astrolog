#!/usr/bin/env python3
"""Source in this tree is LF. Fail on any carriage return in it.

The tree used to be a 55/53 split -- upstream's files CRLF, this fork's own
LF -- with a per-file rule about preserving that. It was enforced by
whoever was editing remembering to, which is not enforcement: it broke in
work log item 145 (one line of express.cpp) and again in 158 (43 lines of
calc.cpp), both times caught only by an ad-hoc assertion someone thought
to write that day.

Nothing needed CRLF. Converting the tree left all 64 object files
byte-identical -- 31 from g++ 11 on Linux, 33 from mingw g++ 10 for
Windows -- and windres produces the same .res from either input.

Two things keep it that way: `.gitattributes` marks everything `-text`, so
a clone on Windows with the default core.autocrlf=true cannot rewrite the
tree on checkout, and this audit fails if a CR appears anyway.

Exit 0 when clean, 1 on any CR in a tracked text file.
"""

import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Everything .gitattributes exempts: binaries, and the files Windows or
# VMS tooling reads in its own format. Keep the two lists in step.
SKIP = ('.se1', '.ttf', '.pdf', '.docx', '.rtf', '.png', '.ico', '.bmp',
        '.exe', '.res', '.o',
        '.sln', '.vcproj', '.vcxproj', '.rc', '.def', '.url', '.com',
        '.as', '.csv', '.htm')


def main():
    out = subprocess.run(['git', 'ls-files', '-z'], cwd=ROOT,
                         stdout=subprocess.PIPE, check=True).stdout
    bad, cFile = [], 0
    for name in out.split(b'\0'):
        if not name:
            continue
        f = name.decode('utf-8', 'replace')
        if (f.lower().endswith(SKIP) or f.startswith('font/') or
                f in ('sefstars.txt', 'seorbel.txt')):
            continue
        path = os.path.join(ROOT, f)
        if not os.path.isfile(path):
            continue
        with open(path, 'rb') as fh:
            data = fh.read()
        cFile += 1
        if b'\r' in data:
            bad.append((f, data.count(b'\r'), data.count(b'\n')))
    if bad:
        print('carriage returns in %d of %d tracked text files:' %
              (len(bad), cFile))
        for f, cr, lf in bad:
            print('  %-24s CR=%d LF=%d' % (f, cr, lf))
        print('\nSource in this tree is LF (work log item 159). If one of')
        print('these is source, strip its CRs. If it is a binary, or a file')
        print('Windows tooling or the program itself parses, add it to SKIP')
        print('and to .gitattributes instead -- a sweep that stripped CRs')
        print('from .se1 and .ttf files corrupted 28 of them once already.')
        return 1
    print('line endings clean: %d tracked text files, no carriage returns'
          % cFile)
    return 0


if __name__ == '__main__':
    sys.exit(main())
