#!/usr/bin/env python3
#
# Astrolog (Version 8.00) File: tools/text-chart-diff.py
#
# Stack matching captures from tools/text-chart-capture.sh into one image
# per chart type, Qt above the Windows build, so the two can actually be
# compared. Prints a colour-count summary alongside, which is the cheap
# way to catch a chart that came out in the wrong mode entirely.
#
#   tools/text-chart-capture.sh wine out/win
#   tools/text-chart-capture.sh qt   out/qt
#   python3 tools/text-chart-diff.py out/win out/qt [out/cmp]
#
# Needs python3-pil.
#
# On reading the colour counts: Astrolog draws graphics charts from a
# 16 colour palette with no antialiasing, so a *graphics* chart lands at
# roughly 16 distinct colours while an antialiased *text* chart runs to
# the hundreds. That inverts the intuition that "more colours" means
# "more was drawn" -- a text chart type that reports ~16 rendered as
# graphics and is almost certainly blank, which is exactly what selecting
# Aspect List with graphics on used to do. Do not use a low count on its
# own as evidence of blankness; open the image.

import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("needs python3-pil: sudo apt install python3-pil")


def NColors(im):
    rgc = im.getcolors(400000)
    return len(rgc) if rgc is not None else -1


def main():
    if len(sys.argv) < 3:
        sys.exit("usage: %s <windows-dir> <qt-dir> [out-dir]" % sys.argv[0])
    szWin, szQt = sys.argv[1], sys.argv[2]
    szOut = sys.argv[3] if len(sys.argv) > 3 else "out/cmp"
    os.makedirs(szOut, exist_ok=True)

    rgsz = sorted(sz[:-4] for sz in os.listdir(szQt) if sz.endswith(".png"))
    if not rgsz:
        sys.exit("no captures in %s" % szQt)

    print("%-14s %-22s %-22s" % ("chart", "qt (colours)", "windows (colours)"))
    for sz in rgsz:
        szPathQt = os.path.join(szQt, sz + ".png")
        szPathWin = os.path.join(szWin, sz + ".png")
        if not os.path.exists(szPathWin):
            print("%-14s %-22s %s" % (sz, NColors(Image.open(szPathQt).convert("RGB")),
                                      "-- not captured --"))
            continue
        imQt = Image.open(szPathQt).convert("RGB")
        imWin = Image.open(szPathWin).convert("RGB")

        # Windows draws its own title bar and menu bar into the capture at
        # a different height than Qt's, so a fixed crop is the honest way
        # to line the text bodies up rather than pretending pixel origins
        # match. Tune per build if either chrome changes.
        cx = min(imQt.width, imWin.width, 900)
        cyQt, cyWin = 300, 300
        a = imQt.crop((0, 0, cx, min(cyQt, imQt.height)))
        b = imWin.crop((0, 15, cx, min(cyWin + 15, imWin.height)))

        c = Image.new("RGB", (cx, a.height + b.height + 8), (90, 90, 90))
        c.paste(a, (0, 0))
        c.paste(b, (0, a.height + 8))
        c.save(os.path.join(szOut, sz + ".png"))

        print("%-14s %-22d %-22d" % (sz, NColors(imQt), NColors(imWin)))

    print("\nstacked comparisons in %s (Qt on top, Windows below)" % szOut)
    print("Layout is what to compare: column positions, row spacing, where\n"
          "each field starts. Content differs whenever the two builds hold\n"
          "different chart data, and the header gains a second line whenever\n"
          "a chart *name* is set (charts1.cpp:91) -- neither is a renderer\n"
          "divergence.")


if __name__ == "__main__":
    main()
