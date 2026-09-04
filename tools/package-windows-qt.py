#!/usr/bin/env python3
"""Stage the Windows package: the Qt build, its runtime, and the data.

    python tools/package-windows-qt.py <dist-dir> [--exe astrolog.exe]

Runs on the Windows runner that built astrolog.exe, because only that
machine has the Qt it was linked against and the compiler runtime it
needs. Everything else -- the zip, the installer, the Wine-driven
install/uninstall check -- happens on Linux afterwards, from this tree,
in .github/workflows/windows-qt.yml.

This is the Windows package since 2026-09-04. Until then the release
shipped the Win32 build from wdriver.cpp, cross-compiled with mingw and
staged by tools/package.sh, and the Qt build for Windows was a nightly
artifact nobody downloaded. The maintainer's decision was to make the
Qt port the one interface on every platform; the Win32 build stays as
the behavioural oracle the differential harnesses compare against, and
is no longer what a user gets.

WHAT SHIPS, and why each entry is here:

  astrolog.exe        the Qt build, /SUBSYSTEM:WINDOWS so no console
                      opens beside it. Named astrolog.exe rather than
                      astrolog-qt.exe: it is Astrolog now.
  Qt6*.dll            whatever windeployqt decides the binary imports.
                      Asking the tool beats keeping a list.
  platforms/          qwindows.dll from windeployqt, which is the only
                      plugin that can show a window on a desktop; PLUS
                      qoffscreen.dll and qminimal.dll, which windeployqt
                      does not ship and the suite needs
                      (QT_QPA_PLATFORM=offscreen). Their absence cost a
                      run each -- see tools/win-vm-suite.sh.
  msvcp140.dll,       the MSVC runtime. /MD links against it and the
  vcruntime140*.dll   runner has it; a user's machine may not, and a
                      missing one is an instant exit with no dialog
                      (0xC0000135). windeployqt's --compiler-runtime
                      needs VCToolsRedistDir from vcvars, which a
                      separate workflow step does not have, so this
                      finds the redist directory through vswhere itself.
  ephem/ font/        as every other package. icons/ too, and astrlog1.ico:
  icons/              IconAstrologQt() tries icons/astrolog{16,32,48}.png
                      beside the executable and falls back to the .ico,
                      and the first Windows run of the suite lost 8 of
                      its 15 failures to having NEITHER.
  *.as, sefstars.txt, the data, the same list tools/package.sh ships and
  seorbel.txt,        tools/ci-verify-package.sh requires.
  astexo.csv, earth.bmp, *.htm
  SHA256SUMS          LF, no BOM, the format sha256sum -c reads. Written
                      here and verified by ci-verify-package.sh, which
                      never writes one.

Overrides for a dry run on a machine with no Qt and no MSVC, which is
how the Linux half of the pipeline is tested without a Windows runner:
ASTROLOG_WINDEPLOYQT names the deploy command, ASTROLOG_VCREDIST_DIR the
directory holding the CRT DLLs. Both default to the real thing.
"""

import glob
import hashlib
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

DATA_FILES = ["astrolog.as", "atlas.as", "timezone.as",
              "sefstars.txt", "seorbel.txt", "astexo.csv", "earth.bmp",
              "astrlog1.ico",
              "astrolog.htm", "changes.htm", "license.htm"]
DATA_DIRS = ["ephem", "font", "icons"]
EXTRA_PLUGINS = ["qoffscreen.dll", "qminimal.dll"]
CRT_DLLS = ["msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll",
            "vcruntime140.dll", "vcruntime140_1.dll"]
CRT_REQUIRED = ["msvcp140.dll", "vcruntime140.dll"]


def die(msg):
    print(msg)
    sys.exit(1)


def find_vcredist():
    forced = os.environ.get("ASTROLOG_VCREDIST_DIR")
    if forced:
        return forced
    vswhere = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                           "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not os.path.exists(vswhere):
        return ""
    try:
        vs = subprocess.check_output(
            [vswhere, "-latest", "-products", "*",
             "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
             "-property", "installationPath"], text=True).strip()
    except subprocess.CalledProcessError:
        return ""
    # VC/Redist/MSVC/<toolset>/x64/Microsoft.VC143.CRT/ -- the toolset
    # directory name moves with every VS update, so glob for it and take
    # the newest.
    cands = sorted(glob.glob(os.path.join(vs, "VC", "Redist", "MSVC", "*", "x64",
                                          "Microsoft.VC*.CRT")))
    return cands[-1] if cands else ""


def main(argv):
    if len(argv) < 2:
        print(__doc__.split("\n\n")[0])
        print("usage: package-windows-qt.py <dist-dir> [--exe astrolog.exe]")
        return 2
    dist = argv[1]
    exe = argv[argv.index("--exe") + 1] if "--exe" in argv else "astrolog.exe"
    if not os.path.isfile(exe):
        die("no such binary: %s -- build it first (tools/msvc-build-qt.cmd)" % exe)
    qtdir = os.environ.get("QTDIR", "")
    deploy = os.environ.get("ASTROLOG_WINDEPLOYQT") or os.path.join(qtdir, "bin", "windeployqt.exe")
    if not qtdir and not os.environ.get("ASTROLOG_WINDEPLOYQT"):
        die("QTDIR is not set -- point it at the Qt this binary was linked against")

    if os.path.isdir(dist):
        shutil.rmtree(dist)
    os.makedirs(dist)

    # 1. The program, under the name it ships with.
    shutil.copy2(exe, os.path.join(dist, "astrolog.exe"))

    # 2. Its Qt runtime, as windeployqt sees it.
    cmd = [deploy, "--release", "--no-translations",
           "--no-system-d3d-compiler", "--no-opengl-sw",
           os.path.join(dist, "astrolog.exe")]
    print("== " + " ".join(cmd))
    rc = subprocess.call(cmd)
    if rc != 0:
        die("windeployqt failed (exit %d)" % rc)

    # 3. The plugins windeployqt does not bring and the suite needs.
    plat = os.path.join(dist, "platforms")
    os.makedirs(plat, exist_ok=True)
    for p in EXTRA_PLUGINS:
        src = os.path.join(qtdir, "plugins", "platforms", p)
        if not os.path.isfile(src):
            die("no %s under QTDIR -- the suite cannot run without it" % src)
        shutil.copy2(src, plat)

    # 4. The compiler runtime.
    missing = [d for d in CRT_REQUIRED if not os.path.isfile(os.path.join(dist, d))]
    if missing:
        redist = find_vcredist()
        if not redist or not os.path.isdir(redist):
            die("the MSVC runtime (%s) is not staged and no redist directory was "
                "found -- the program would exit at once on a machine without "
                "Visual Studio" % ", ".join(missing))
        print("== compiler runtime from " + redist)
        for d in CRT_DLLS:
            src = os.path.join(redist, d)
            if os.path.isfile(src) and not os.path.isfile(os.path.join(dist, d)):
                shutil.copy2(src, dist)
        still = [d for d in CRT_REQUIRED if not os.path.isfile(os.path.join(dist, d))]
        if still:
            die("redist directory %s has no %s" % (redist, ", ".join(still)))

    # 5. The data.
    for d in DATA_DIRS:
        shutil.copytree(os.path.join(ROOT, d), os.path.join(dist, d))
    for f in DATA_FILES:
        shutil.copy2(os.path.join(ROOT, f), dist)
    if os.path.exists(os.path.join(dist, "nrvate.as")):
        die("nrvate.as is in the package")

    # 6. The manifest, in sha256sum's own format so "sha256sum -c" reads
    # it: hash, two spaces, ./path with forward slashes, sorted bytewise
    # the way "find | sort" would. LF only -- a CRLF manifest fails on
    # every line on every platform, and open() on Windows would write
    # one without newline="".
    entries = []
    for base, dirs, files in os.walk(dist):
        for f in files:
            full = os.path.join(base, f)
            rel = "./" + os.path.relpath(full, dist).replace(os.sep, "/")
            h = hashlib.sha256()
            with open(full, "rb") as fh:
                for chunk in iter(lambda: fh.read(1 << 20), b""):
                    h.update(chunk)
            entries.append((rel, h.hexdigest()))
    entries.sort(key=lambda e: e[0].encode())
    with open(os.path.join(dist, "SHA256SUMS"), "w", newline="\n", encoding="ascii") as fh:
        for rel, digest in entries:
            fh.write("%s  %s\n" % (digest, rel))

    n = len(entries) + 1
    size = sum(os.path.getsize(os.path.join(b, f))
               for b, _, fs in os.walk(dist) for f in fs)
    print("== staged %s: %d files, %.1f MB" % (dist, n, size / 1e6))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
