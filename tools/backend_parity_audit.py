#!/usr/bin/env python3
"""Check that every setting the Windows GUI acts on, the Qt GUI acts on too.

    python3 tools/backend_parity_audit.py

The Windows build is this fork's behavioural oracle, and the shape a bug
hides in -- CLAUDE.md says so in as many words -- is "a WIN-only branch
with no QT in it". The dialogs are covered from the resource side by
rc_field_audit and its neighbours. This covers the other half: the
`us.*` and `gs.*` fields the Windows *driver and dialogs* read or write
outside any control binding, which is where command handlers and
enforcement points live.

It found two on the run that produced it, both switches that silently did
nothing in this build:

  * us.fNoQuit     -- "-0q", do not allow quitting. Windows refuses
                      WM_CLOSE; the Qt port referenced the flag nowhere,
                      so File / Exit, the Alt+F4 macro and the window
                      manager's button all closed the window.
  * us.fNoGraphics -- "-0X", do not allow graphics. Windows forces
                      us.fGraphics off after every command; the Qt port
                      did not, so the View menu turned it back on.

Both are in the "-0" lockdown family, which is one-way by design, so a
backend that fails to enforce one does not merely differ from the other:
it has a switch that does nothing at all.

The allowlist below is the point of the exercise. Every entry carries the
reason it is NOT a gap, and an entry with no reason is not an entry --
this is the same discipline tools/inert_option_audit.py uses, and for the
same reason: a bare list of names becomes a place to put things that
should have been fixed.
"""

import re
import sys

WIN = ["wdriver.cpp", "wdialog.cpp"]
QT = ["qtdriver.cpp", "qtdialog.cpp"]
FIELD = re.compile(r"\b((?:us|gs)\.[A-Za-z_][A-Za-z0-9_]*)")

# Named in the Windows GUI, absent from the Qt one, and correct that way.
ALLOW = {
    # The Help menu's "List Signs", "List Objects" and so on. Windows sets
    # each flag by hand; rgchartmode[] (xscreen.cpp) maps every one of
    # these modes to its flag, and the Qt port goes through
    # SetChartModeQt(gSign) instead, so it never names the flag. Verified
    # by reading that table, which has {gSign, &us.fSign} and a row for
    # each of the others.
    "us.fSign": "set through rgchartmode[] by SetChartModeQt(gSign)",
    "us.fObject": "set through rgchartmode[] by SetChartModeQt(gObject)",
    "us.fAspect": "set through rgchartmode[] by SetChartModeQt(gAspect)",
    "us.fConstel": "set through rgchartmode[] by SetChartModeQt(gConstel)",
    "us.fOrbitData": "set through rgchartmode[] by SetChartModeQt(gOrbit2)",
    "us.fRay": "set through rgchartmode[] by SetChartModeQt(gRay)",
    "us.fMeaning": "set through rgchartmode[] by SetChartModeQt(gMeaning)",
    "us.fCredit": "set through rgchartmode[] by SetChartModeQt(gCredit)",
    "us.fSwitch": "set through rgchartmode[] by SetChartModeQt(gSwitch)",
    "us.fSwitchRare": "set through rgchartmode[] by SetChartModeQt(gObscure)",
    "us.fKeyGraph": "set through rgchartmode[] by SetChartModeQt(gKeystroke)",

    # Windows overwrites this with the client height in character rows
    # while capturing text, then restores it, so its own pager breaks the
    # listing into screenfuls. The Qt capture writes a file and reads it
    # back whole, and the text window has a real scrollbar -- which is
    # also why the four text-scrolling accelerators are deliberately
    # unbound here (QT_GUI_PLAN.md item 2).
    "us.nScrollRow": "Windows' text pager; Qt has a QScrollArea instead",

    # us.nCharsetOut used to be listed here, on the reasoning that
    # Windows reads it only to pick CF_OEMTEXT over CF_TEXT and a Qt
    # clipboard carries a QString. That reading is what hid a bug: the
    # conversion Windows delegates to the paste target has to happen in
    # the port instead, and it did not, so every box edge of a text wheel
    # arrived as U+FFFD. CaptureTextChartQt() decodes by the codepage the
    # file was written in now, which means the field is named in live Qt
    # code and the entry went stale -- this audit said so, which is the
    # stale check earning its place.

    # Not a setting Windows acts on at all: it appears once, as the upper
    # ADDRESS BOUND of the second ClearB() range,
    # "(pbyte)&us.fLoop - (pbyte)&us.fCredit". The second check below
    # reads that same arithmetic on purpose; this entry is only here so
    # the field's name does not read as a missing behaviour.
    "us.fLoop": "an address bound in ClearB(), not a setting acted on",
}


# Comments are stripped first, and that is not tidiness. The first run of
# this script reported us.nCharsetOut as "allowlisted but no longer
# missing" because a comment in qtdriver.cpp had just started explaining
# what that field does. An audit a comment can satisfy is not an audit:
# the whole claim here is that the Qt backend ACTS on the setting.

def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
    return re.sub(r"//[^\n]*", " ", src)


def fields(paths):
    out = set()
    for path in paths:
        try:
            with open(path, newline="") as f:
                src = f.read()
        except IOError as e:
            print("cannot read %s: %s" % (path, e))
            return None
        out |= set(FIELD.findall(strip_comments(src)))
    return out


# The second check, and the one that found a bug rather than a gap.
#
# Windows clears the chart-type flags by zeroing two raw byte ranges of
# the US struct -- us.fListing through us.fVelocity, and us.fCredit
# through us.fLoop (wdriver.cpp:1164). SetChartModeQt() instead walks
# rgchartmode[] and clears each flag it names, which is the better shape
# but is NOT the same set: a flag that lies in one of those ranges and
# has no table row is cleared on Windows and not here.
#
# Two did. fAtlasLook ("-N") and fZoneChange ("-Nz") are additive text
# listings -- charts1.cpp appends each to whatever the chart already
# printed -- so left set they stapled an atlas dump onto every chart
# picked afterwards, with no menu item in this port to turn either off.
#
# So the ranges are read out of the struct here rather than transcribed,
# and every flag in them has to be either an rgchartmode[] row or named
# in SetChartModeQt(). A flag added upstream between those bounds fails
# this rather than going quietly missing.

BOUNDS = [("fListing", "fVelocity"), ("fCredit", "fLoop")]


def chart_flag_gaps():
    with open("astrolog.h", newline="") as f:
        h = f.read()
    m = re.search(r"typedef struct _UserSettings \{(.*?)\n\} US;", h, re.S)
    if m is None:
        return None, "cannot find the US struct in astrolog.h"
    names = []
    for line in m.group(1).split("\n"):
        t = re.sub(r"//.*", "", line).strip()
        mm = re.match(r"^(?:CONST\s+)?(\w+)\s+\*?(\w+)(\[[^\]]*\])?\s*;$", t)
        if mm:
            names.append(mm.group(2))
    inRange = []
    for lo, hi in BOUNDS:
        if lo not in names or hi not in names:
            return None, "cannot find %s..%s in the US struct" % (lo, hi)
        inRange += names[names.index(lo):names.index(hi)]

    with open("xscreen.cpp", newline="") as f:
        rows = set(re.findall(r"\{g\w+,\s*&us\.(\w+)\}", f.read()))
    if not rows:
        return None, "cannot find rgchartmode[] rows in xscreen.cpp"

    with open("qtdriver.cpp", newline="") as f:
        src = strip_comments(f.read())
    m = re.search(r"void SetChartModeQt\(int mode\)\s*\{(.*?)\n\}", src, re.S)
    if m is None:
        return None, "cannot find SetChartModeQt() in qtdriver.cpp"
    cleared = set(re.findall(r"us\.(\w+)", m.group(1)))

    return [f for f in inRange if f not in rows and f not in cleared], None


# The third axis, and the one with the best record: SHARED-CORE FUNCTIONS
# the Windows GUI calls and the Qt GUI never does. It has found three
# bugs -- the chart list's AstroExpression filter (plan item 42), the
# restriction "Recall" button handing back compiled defaults instead of
# the user's, and that same dialog's filter never being made permanent.
#
# Two filters keep the noise down to something an allowlist can carry.
# Only functions declared in extern.h count, so Win32 API calls are out;
# and functions DEFINED in wdriver.cpp or wdialog.cpp are out too, since
# those are Win32 by construction -- that is what removes Dlg*, SetEdit*,
# WndProc, RedoMenu and the rest, 72 names down to 17.

ALLOWFN = {
    "CchSz": "string length; the Qt code uses QString",
    "ClearB": "memset over a struct range; Qt clears through tables",
    "PAllocate": "Astrolog's allocator; Qt objects are new/delete",
    "DeallocateP": "ditto",
    "ConvertSzToLatin": "Win32 dialogs are ANSI; Qt controls are Unicode",
    "DrawClearScreen": "draws through gi.qpaint, which only exists for the "
                       "length of a redraw -- ClearScreenQt() fills the "
                       "buffer instead, which is what it would have done",
    "EnsureRay": "derives rgSignRay2[] from rgSignRay[]. Windows calls it "
                 "when the ray restriction is lifted; every consumer of "
                 "that table already calls it first (charts0, charts1, "
                 "xcharts2, intrpret), so the dialog call is belt and "
                 "braces. Checked rather than assumed",
    "FBmpDrawBack": "Win32 background blitting; xdevice.cpp has Qt paths",
    "FBmpShrinkToWin": "takes HDCs",
    "FProcessSwitchFile": "startup reads astrolog.as in astrolog.cpp, "
                          "which both non-Windows builds share",
    "InitProgram": "called from astrolog.cpp's main, shared",
    "FinalizeProgram": "ditto",
    "InitRestrictions": "called from astrolog.cpp's main under #ifdef QT, "
                        "at the point wdriver.cpp calls it -- work log "
                        "item 194",
    "OpenDir": "Win32 directory walk; ShowOpenChartDirDialogQt uses QDir",
    "PrintError": "PrintWarningQt() puts both kinds in a message box",
    "PrintNotice": "ditto",
    "ResizeWindowToChart": "#ifdef WINANY only; ResizeWindowToChartQt()",
}


def function_gaps():
    with open("extern.h", newline="") as f:
        core = set(re.findall(r"\b(\w+)\s*P\(\(", f.read()))
    if not core:
        return None, "cannot find any P(( declarations in extern.h"

    def calls(paths):
        out = set()
        for path in paths:
            with open(path, newline="") as f:
                out |= set(re.findall(r"\b(\w+)\s*\(", strip_comments(f.read())))
        return out

    wsrc = ""
    for path in WIN:
        with open(path, newline="") as f:
            wsrc += strip_comments(f.read())
    here = set(re.findall(r"^\s*(?:\w[\w *]*?)\b(\w+)\s*\([^;]*\)\s*$",
                          wsrc, re.M))
    missing = sorted(((calls(WIN) & core) - (calls(QT) & core)) - here)
    return [f for f in missing if f not in ALLOWFN], None


def main():
    gapsFn, err = function_gaps()
    if err is not None:
        print(err)
        return 1
    if gapsFn:
        for f in gapsFn:
            print("  %-20s called by the Windows GUI and by nothing in the "
                  "Qt one" % (f + "()"))
        print("\nEither the Qt backend is missing behaviour the oracle has,")
        print("or it does the same job another way -- in which case add it")
        print("to ALLOWFN WITH THE REASON.")
        return 1

    gapsChart, err = chart_flag_gaps()
    if err is not None:
        print(err + " -- this audit reads all three by shape.")
        return 1
    if gapsChart:
        for f in gapsChart:
            print("  us.%-16s cleared by Windows on a chart-type change, "
                  "and by nothing here" % f)
        print("\nWindows zeroes a raw struct range; SetChartModeQt() walks")
        print("rgchartmode[]. A flag inside that range with no table row is")
        print("cleared there and not here. Add it to SetChartModeQt(), or")
        print("give it a table row if it really is a chart mode.")
        return 1

    win, qt = fields(WIN), fields(QT)
    if win is None or qt is None:
        return 1
    if not win or not qt:
        print("no us./gs. fields parsed -- has a file been renamed? "
              "This audit reads the four GUI sources by name.")
        return 1

    missing = sorted(win - qt)
    gaps = [f for f in missing if f not in ALLOW]
    stale = sorted(f for f in ALLOW if f not in missing)

    if gaps:
        for f in gaps:
            print("  %-18s acted on by the Windows GUI and by nothing in "
                  "the Qt one" % f)
        print("\nEither the Qt backend is missing behaviour the oracle has,")
        print("or it reaches the same setting another way -- in which case")
        print("add it to ALLOW in this script WITH THE REASON, the way the")
        print("chart-mode flags are listed there.")
        return 1

    if stale:
        print("  allowlisted but no longer missing, so the entry is stale: "
              "%s" % " ".join(stale))
        return 1

    print("backend parity clean: %d us./gs. fields in the Windows GUI, "
          "%d also in the Qt GUI, %d allowlisted with reasons; and every "
          "chart-type flag Windows clears is cleared here too, and "
          "every shared-core function it calls is called here or "
          "allowlisted (%d)"
          % (len(win), len(win) - len(missing), len(ALLOW), len(ALLOWFN)))
    return 0


sys.exit(main())
