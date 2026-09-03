#!/usr/bin/env python3
"""The staged Qt-on-Windows tree is complete and runnable.

    python3 tools/qt_windows_dist_audit.py dist

The nightly assembles this tree with windeployqt, copies two extra
platform plugins and the icons into it, uploads it, and nothing ever looks
at the result. Both of the additions are there because their absence
already cost a run, and both are recorded in the workflow's own comments:

  * windeployqt ships only the "windows" platform plugin. Without
    "offscreen" the suite exits at once with "Could not find the Qt
    platform plugin"; with only "windows" it blocks forever, because a
    guestcontrol process runs in session 0 and has no desktop. The second
    was found by a ten-minute hang.

  * The icons were in neither of the two places IconAstrologQt() looks,
    which cost 8 of the 15 failures in the first Windows run of the suite
    -- a packaging gap reported as a port defect.

Written in Python rather than sh because it runs on a Windows runner
alongside the other audits. It checks the STAGED TREE, not the zip: the
artifact upload is what turns one into the other.
"""
import os, sys, glob

d = sys.argv[1] if len(sys.argv) > 1 else "dist"
if not os.path.isdir(d):
    print(f"no such directory: {d}")
    sys.exit(2)

bad = []


def need(rel, why):
    if not os.path.exists(os.path.join(d, rel)):
        bad.append(f"{rel}  -- {why}")


for exe in ("astrolog-qt.exe", "astrolog-qt-test.exe"):
    need(exe, "the program and the -DQTTEST build the suite runs")

# The Qt runtime windeployqt is supposed to have brought.
for dll in ("Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll"):
    need(dll, "Qt runtime; windeployqt did not run or did not finish")

# Both plugins, and the reason each is here is a run that was lost.
need(os.path.join("platforms", "qwindows.dll"),
     "the only plugin that can show a window on a real desktop")
need(os.path.join("platforms", "qoffscreen.dll"),
     "the suite sets QT_QPA_PLATFORM=offscreen; without this it exits at "
     "once, and with only qwindows it blocks forever in session 0")

# IconAstrologQt() tries icons/astrolog{16,32,48}.png beside the
# executable, then falls back to astrlog1.ico. The artifact once had
# NEITHER.
for n in (16, 32, 48):
    need(os.path.join("icons", f"astrolog{n}.png"),
         "IconAstrologQt() asserts this exact size rather than a scaled blur")
need("astrlog1.ico", "the fallback when the PNGs are missing")

# The ephemeris, exactly as many files as the tree ships. Chiron alone
# cannot notice the 20 esoteric-body files going missing: seas_18.se1 was
# always there.
here = len(glob.glob(os.path.join(os.path.dirname(__file__), "..", "ephem", "*.se1")))
there = len(glob.glob(os.path.join(d, "ephem", "*.se1")))
if here and there != here:
    bad.append(f"ephem/  -- {there} .se1 files staged, this tree ships {here}")

if bad:
    print(f"THE QT-ON-WINDOWS TREE IS INCOMPLETE ({d}):")
    for b in bad:
        print("  " + b)
    print("== It would upload, download, and fail on someone's desktop.")
    sys.exit(1)
print(f"qt-on-windows dist ok: both binaries, Qt runtime, 3 platform "
      f"plugins, icons, {there} ephemeris files")
