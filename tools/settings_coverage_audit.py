#!/usr/bin/env python3
"""Every setting a settings dialog writes must survive Save Program Settings.

The writer, FOutputSettings() in io.cpp, is a hand-maintained list of
sprintf2() lines. Nothing has ever compared it against the dialogs, and the
gap was large: a sweep on 2026-09-06 found 28 us/gs fields that a settings
dialog assigns and the writer never names. Eleven were real losses -- the
harmonic factor, the dwad level, the solar chart object, the 3D house
plane, the required aspect object, the star sort order, the star dot and
name bits, the wheel rotation object, and three moon settings -- each of
them a control a user sets, saves, and finds back at its default next
launch. Six were false alarms and five cannot be written at all; both sets
are in ALLOW below, each with the reason measured rather than assumed.

The dialogs that count are the ones whose resource caption says Settings
(plus the object and colour ones, which are settings by any other name).
Transits, Progressions, Set Chart Info and the chart list cast or edit one
chart; what they set is not a saved setting and never was.

  python3 tools/settings_coverage_audit.py
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# rgctl<Name> table -> what astrolog.rc calls that dialog. A dialog is in
# scope when its caption describes a preference rather than a chart.
SETTINGS = {
    "Aspect":    "Aspect Settings",
    "Calc":      "Calculation Settings",
    "Chart":     "Chart Settings",
    "Color":     "Set Colors",
    "Custom":    "Object Customization",
    "CustomS":   "Fixed Star Customization",
    "Display":   "Display Settings",
    "File":      "File Settings",
    "Graphics":  "Graphics Settings",
    "Object":    "Object Settings",
    "Object2":   "More Object Settings",
    "ObjectM":   "Planetary Moon Object Settings",
    "ObjectSel": "Object Selections",
}
CASTS = {
    "Command":  "Enter Command Line -- types a command line, saves nothing",
    "Default":  "Default Chart Info -- the -z family, written as chart info",
    "Info":     "Set Chart Info -- one chart's own data",
    "InfoAll":  "Charts #3 through #6 -- the same, for the other slots",
    "List":     "Chart List -- the list, not the settings",
    "Progress": "Progressions -- casts one progressed chart",
    "Transit":  "Transits -- casts one transit chart",
}

# Fields a settings dialog assigns that FOutputSettings() does not name, and
# why each is allowed to be missing. Every reason here was measured.
ALLOW = {
    # Written as one packed value the switch can only set whole.
    "gs.nFontTxt": 'carried by gs.nFontAll, which ":YXf" writes',
    "gs.nFontSig": 'carried by gs.nFontAll, which ":YXf" writes',
    "gs.nFontHou": 'carried by gs.nFontAll, which ":YXf" writes',
    "gs.nFontObj": 'carried by gs.nFontAll, which ":YXf" writes',
    "gs.nFontAsp": 'carried by gs.nFontAll, which ":YXf" writes',
    "gs.nFontNak": 'carried by gs.nFontAll, which ":YXf" writes',
    # No switch spelling can carry these without changing something else.
    "us.rRatio":
        '"-r0 <file1> <file2> [<ratio>]" needs two chart files to name a '
        'ratio, and a settings file has none to give',
    "us.fMoonChartSep":
        'the "80" registry row sets us.fMoonChart as well (SWITCHFLAG.pf2), '
        'so any prefix that carries the flag also turns the moons chart on, '
        'and ":" carries neither',
    "gs.rRot":
        'every switch that sets it (-XX/-XW/-XG/-XP) ends in '
        '"gi.nMode = FSwitchF2(gi.nMode == <mode>) * <mode>", which zeroes '
        'the chart mode unless it already was that one',
    "gs.rTilt": 'same as gs.rRot -- -XX and -XG set both',
    "gs.objTrack": 'same as gs.rRot -- -XZ zeroes gi.nMode the same way',
    "us.nEphemYears":
        '"-EY 0" is refused (FErrorValN, i < 1) and 0 is the value the '
        '"Ephemeris Is for Entire Year" box unticked leaves behind, while '
        '":Ey" reads us.fEphemeris and so is not deterministic',
    "gs.fBackDraw":
        'it says a background bitmap loaded with "-XI <file>" is showing, '
        'and the file name is not saved either, so the flag alone restores '
        'nothing',
    # The largest group, and the structural one: these are display
    # preferences spelt as a sub-letter of a CHART TYPE switch, so the
    # handler toggles the preference and the chart type from the same
    # prefix. "=" or "-" would select that chart on load, and ":" carries
    # neither flag, so there is no spelling that saves them alone. The
    # settings format is the command line, and the command line was never
    # asked to separate the two.
    "us.fWheelReverse": 'NSww: "-w0" toggles it and us.fWheel',
    "us.fGridConfig": 'NSwg: "-g0" toggles it and us.fGrid',
    "us.fGridMidpoint": 'NSwg: "-gm" toggles it and us.fGrid',
    "us.fAspSummary": 'NSwa: "-a0" toggles it and us.fAspList',
    "us.fDistance": 'NSwa/NSwg: "-ad"/"-gd" toggle it and the chart type',
    "us.fMidSummary": 'NSwm: "-m0" toggles it and us.fMidpoint',
    "us.fPrimeVert": 'NSwZ: "-Z0" toggles it and us.fHorizon',
    "us.fLatitudeCross": 'NSwL: "-L0" toggles it and us.fAstroGraph',
    "us.fArabicFlip": 'NSwP: "-P0" toggles it and us.fArabic',
    "us.fCalendarYear":
        'the "Ky" registry row sets us.fCalendar as well (SWITCHFLAG.pf2)',
    "us.fInfluenceSign":
        'the "j0" registry row sets us.fInfluence as well',
    "us.fSectorApprox":
        'the "l0" registry row sets us.fSector as well',
    "gs.fSouth":
        'NSwXX/NSwXG/NSwXP: the "0" suffix toggles it, and those handlers '
        'zero gi.nMode the way gs.rRot\'s do',
    "gs.fMollweide": 'NSwXW: "-XW0" toggles it and zeroes gi.nMode',
}

FIELD = re.compile(r"\b(us|gs)\.([A-Za-z_][A-Za-z0-9_]*)\s*=(?!=)")
# Most checkboxes are not assigned in code at all: they are RCFLAG rows,
# {"<control>", <index>, &us.fWhatever, <invert>}, that RcLoadFlagsQt() and
# NRcStoreFlagsQt() drive both ways. Missing them cost this audit its first
# run, which reported us.fSolarWhole as unset by any dialog.
RCFLAG = re.compile(r"&\s*(us|gs)\.([A-Za-z_][A-Za-z0-9_]*)\s*,\s*f(?:True|False)\s*\}")
FUNC = re.compile(r"^[A-Za-z_].*\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")
BUILD = re.compile(r"RcBuildDialogQt\(&\w+,\s*rgctl([A-Za-z0-9]+)")


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def dialog_fields(path):
    """Map every us/gs field assigned in a dialog function to its dialog."""
    out = {}
    fn = None
    body = {}
    order = []
    for line in open(path, encoding="utf-8"):
        m = FUNC.match(line)
        if m and "(" in line and not line.startswith("//"):
            fn = m.group(1)
            if fn not in body:
                body[fn] = []
                order.append(fn)
        if fn is not None:
            body[fn].append(line)
    for fn in order:
        text = strip_comments("".join(body[fn]))
        tables = set(BUILD.findall(text))
        if not tables:
            continue
        for pre, name in FIELD.findall(text) + RCFLAG.findall(text):
            out.setdefault("%s.%s" % (pre, name), set()).update(tables)
    return out


def writer_names(path):
    text = open(path, encoding="utf-8").read()
    i = text.index("flag FOutputSettings()")
    j = text.index("\n}\n", i)
    return set(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", strip_comments(text[i:j])))


def main():
    fields = dialog_fields(os.path.join(HERE, "qtdialog.cpp"))
    written = writer_names(os.path.join(HERE, "io.cpp"))
    known = set(SETTINGS) | set(CASTS)

    bad = []
    unknown = sorted({t for ts in fields.values() for t in ts} - known)
    for table in unknown:
        bad.append("dialog rgctl%s is in neither SETTINGS nor CASTS; say "
                   "which it is" % table)

    missing = []
    for field, tables in sorted(fields.items()):
        if not (tables & set(SETTINGS)):
            continue
        if field.split(".", 1)[1] in written:
            continue
        if field in ALLOW:
            continue
        missing.append((field, sorted(tables & set(SETTINGS))))
    for field, tables in missing:
        bad.append("%s is set by %s and FOutputSettings() never writes it"
                   % (field, ", ".join(SETTINGS[t] for t in tables)))

    # An allowlist entry that no longer names a missing field is stale: the
    # writer grew a line for it, or the dialog stopped setting it.
    for field in sorted(ALLOW):
        tables = fields.get(field, set())
        if not (tables & set(SETTINGS)):
            bad.append("ALLOW names %s, which no settings dialog sets any "
                       "more -- drop the entry" % field)
        elif field.split(".", 1)[1] in written:
            bad.append("ALLOW names %s, which FOutputSettings() now writes "
                       "-- drop the entry" % field)

    if bad:
        for line in bad:
            print("settings_coverage_audit: " + line)
        return 1
    print("settings_coverage_audit: %d fields set by %d settings dialogs, "
          "all written or explained (%d allowed)"
          % (len([f for f, t in fields.items() if t & set(SETTINGS)]),
             len(SETTINGS), len(ALLOW)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
