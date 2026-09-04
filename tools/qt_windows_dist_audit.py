#!/usr/bin/env python3
"""The staged Windows tree is complete and runnable.

    python3 tools/qt_windows_dist_audit.py dist          # what ships
    python3 tools/qt_windows_dist_audit.py dist --test   # the suite's copy

tools/package-windows-qt.py assembles the tree with windeployqt, adds two
platform plugins, the compiler runtime and the icons, and this looks at
the result before it leaves the runner. Each addition is there because
its absence already cost a run, and each is recorded in that script:

  * windeployqt ships only the "windows" platform plugin. Without
    "offscreen" the suite exits at once with "Could not find the Qt
    platform plugin"; with only "windows" it blocks forever in a session
    with no desktop. The second was found by a ten-minute hang.

  * The icons were in neither of the two places IconAstrologQt() looks,
    which cost 8 of the 15 failures in the first Windows run of the suite
    -- a packaging gap reported as a port defect.

  * The MSVC runtime is on every runner and not on every user's machine;
    a /MD binary without it exits before main() with no dialog.

Written in Python rather than sh because it runs on a Windows runner. It
checks the STAGED TREE, not the zip; tools/ci-verify-zip.sh checks that
the zip matches the tree, on Linux, afterwards.

--test is for the second artifact, the one tools/win-vm-suite.sh takes
into a VM: the same tree plus astrolog-qt-test.exe, the -DQTTEST build.
The shipped tree must NOT carry it, so without --test its presence fails.
"""
import os, sys, glob

args = [a for a in sys.argv[1:] if not a.startswith("--")]
d = args[0] if args else "dist"
want_test = "--test" in sys.argv
if not os.path.isdir(d):
    print(f"no such directory: {d}")
    sys.exit(2)

bad = []


def need(rel, why):
    if not os.path.exists(os.path.join(d, rel)):
        bad.append(f"{rel}  -- {why}")


need("astrolog.exe", "the program, under the name it ships with")
if want_test:
    need("astrolog-qt-test.exe", "the -DQTTEST build the suite runs")
elif os.path.exists(os.path.join(d, "astrolog-qt-test.exe")):
    bad.append("astrolog-qt-test.exe  -- the test build is in the SHIPPED tree")

# The Qt runtime windeployqt is supposed to have brought.
for dll in ("Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll"):
    need(dll, "Qt runtime; windeployqt did not run or did not finish")

# The compiler runtime, which the runner has and a user may not.
for dll in ("vcruntime140.dll", "msvcp140.dll"):
    need(dll, "MSVC runtime; without it the program exits before main()")

# Both plugins, and the reason each is here is a run that was lost.
need(os.path.join("platforms", "qwindows.dll"),
     "the only plugin that can show a window on a real desktop")
need(os.path.join("platforms", "qoffscreen.dll"),
     "the suite sets QT_QPA_PLATFORM=offscreen; without this it exits at "
     "once, and with only qwindows it blocks forever with no desktop")

# IconAstrologQt() tries icons/astrolog{16,32,48}.png beside the
# executable, then falls back to astrlog1.ico. The artifact once had
# NEITHER.
for n in (16, 32, 48):
    need(os.path.join("icons", f"astrolog{n}.png"),
         "IconAstrologQt() asserts this exact size rather than a scaled blur")
need("astrlog1.ico", "the fallback when the PNGs are missing")

# The data every package ships. ci-verify-package.sh checks the same
# list on Linux later; checking it here means a missing file is named on
# the runner that forgot it, not two jobs downstream.
for f in ("astrolog.as", "atlas.as", "timezone.as", "sefstars.txt",
          "seorbel.txt", "astexo.csv", "earth.bmp",
          "astrolog.htm", "changes.htm", "license.htm"):
    need(f, "data every package ships")
need(os.path.join("font"), "the chart fonts")
need("SHA256SUMS", "the manifest tools/package-windows-qt.py writes")
if os.path.exists(os.path.join(d, "nrvate.as")):
    bad.append("nrvate.as  -- the maintainer's personal settings, never shipped")

# The ephemeris, exactly as many files as the tree ships. Chiron alone
# cannot notice the 20 esoteric-body files going missing: seas_18.se1 was
# always there.
here = len(glob.glob(os.path.join(os.path.dirname(__file__), "..", "ephem", "*.se1")))
there = len(glob.glob(os.path.join(d, "ephem", "*.se1")))
if here and there != here:
    bad.append(f"ephem/  -- {there} .se1 files staged, this tree ships {here}")

if bad:
    print(f"THE WINDOWS TREE IS INCOMPLETE ({d}):")
    for b in bad:
        print("  " + b)
    print("== It would upload, download, and fail on someone's desktop.")
    sys.exit(1)
print(f"windows dist ok: astrolog.exe{' + test build' if want_test else ''}, "
      f"Qt runtime, MSVC runtime, platform plugins, icons, data, "
      f"{there} ephemeris files")
