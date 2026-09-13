#!/usr/bin/env python3
"""The git sha a binary was built from, as a compile-time constant.

    python3 tools/gitsha.py gitsha.h

Writes gitsha.h beside astrolog.h defining szVersionGit as the LAST EIGHT
characters of the checkout's HEAD hash, for the About dialog's version
line. Like tools/version.py this exists in python so tools/
msvc-build-qt.cmd can call it identically on the Windows runner.

The write is guarded by a content comparison, because every makefile
reaches this script through a FORCE prerequisite -- it runs on every make
invocation, and writing unconditionally would dirty gitsha.h each time and
recompile qtdialog.cpp forever.

A tree without .git -- a `git archive` export, which is what
tools/build-check.sh's containers and the release packaging start from --
answers empty, and the About line simply shows no bracket. The header is
also not tracked by git for a deeper reason: its content would be the sha
of the very commit carrying it, which cannot be written before that commit
exists. It is regenerated, never committed.
"""

import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    if len(sys.argv) != 2:
        print("usage: gitsha.py <gitsha.h path>", file=sys.stderr)
        return 2
    path = sys.argv[1]
    sha = ""
    try:
        p = subprocess.run(["git", "-C", ROOT, "rev-parse", "HEAD"],
          stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        if p.returncode == 0:
            sha = p.stdout.strip()[-8:]
    except OSError:
        pass
    # Empty for no .git, so the About line falls back to no bracket.
    line = '#define szVersionGit "%s"\n' % (sha if sha else "")
    # Do not touch the file unless it would change: the FORCE prerequisite
    # runs this on every make, and an unconditional write would dirty the
    # mtime of a header qtdialog.o depends on, rebuilding it every time.
    old = None
    if os.path.exists(path):
        old = open(path).read()
    if old != line:
        open(path, "w").write(line)
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())