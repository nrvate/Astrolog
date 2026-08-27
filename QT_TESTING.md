# Seeing what this app actually does

How to answer a question about this program in **under a second**, and the
failure modes that waste hours if you reach for the wrong tool. Written
after a session that reached for the wrong tool repeatedly.

`QT_COMPARING_WITH_WINDOWS.md` covers diffing this build against the real
Windows one. This file is about observing *this* build.

## Always run with the real config

```sh
./astrolog-qt -i nrvate.as          # and the same -i for every tool below
```

`nrvate.as` is the maintainer's own settings file and is the only
realistic test input. It carries `-Yi1 "/swe"`, the path to the ephemeris
collection — **without it every esoteric body reads `???`**, because
`SwissEnsurePath()` (calc.cpp) caches the search path the first time it is
asked, so a `-Yi` set afterwards does nothing at all. It also carries the
restrictions, orbs, aspect set and 13 macro names that make the program
behave the way it does in use. Testing against the stock `astrolog.as`
answers a question nobody has.

## The fast way: a scratch probe inside qttest

**This is the preferred method and it is not close.** A round trip is
about **0.2 seconds**.

`ProbeQt()` in qttest.cpp is a deliberately empty function. Rewrite its
body to ask the program a question, build, run, read the answer, rewrite
it again:

```c
static void ProbeQt()
{
  char sz[cchSzMax];
  int n, t;

  SzObjSelName(sz, 1, 52872);            // anything in the program is here
  printf("52872 -> %s\n", sz);
  printf("back:  %s\n", FObjSelRecall("Okyrhoe", &t, &n) ? "yes" : "no");
}
```

```sh
make -f Makefile.qt.test -j4
env -u DISPLAY QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= \
  ASTROLOG_QT_PROBE=1 ./astrolog-qt-test -i nrvate.as
```

Why it beats driving the UI: **there is no UI**. No display, no window
manager, no input simulation, no focus, no coordinates, no sleeps, nothing
to miss. The probe runs inside the real program after the window, menus
and first chart are up, so every global is live — poke `us`/`gs`/`gi`
directly, call `SetChartModeQt()` or any dialog function, run switches
through `FProcessCommandLine()`.

The body is **scratch**. Rewrite it freely; nothing depends on it. Do not
put assertions in it — those belong in the suite.

For anything visual, save `gi.qim` and then **measure the PNG rather than
looking at it**:

```python
from PIL import Image
from collections import Counter
im = Image.open("probe.png").convert("RGB"); px = im.load(); w, h = im.size
print(Counter(im.getdata()).most_common(4))
for x in range(w-1, 0, -1):                   # find the sidebar edge
    if any(px[x, y] != (0, 0, 0) for y in range(0, h, 5)):
        print("chart width", x+1); break
```

A worked example of the whole loop: "if the GUI shows me Okyrhoe, will it
accept Okyrhoe back?" — probe written, built, run, answered in 0.21s,
across six esoteric bodies at once.

## Rendering charts headlessly

The same idea, already packaged, for when you want the images themselves:

```sh
QTTEXTDIR=out/qt   ./run-qt-tests.sh    # 8 text charts    -> PNG, ~2s
QTGRAPHDIR=out/qtg ./run-qt-tests.sh    # 24 graphics      -> PNG, ~3s
```

Both render and exit without running the assertion suite. `gAspect` and
`gArabic` are absent from the graphics set on purpose: `DrawChartX()` has
no case for either, which is why Windows forces text mode for exactly
those two.

## The console build, for the calculation core

`make` builds the X11 binary, the quickest way to exercise the shared
calculation and text rendering with no GUI in the way:

```sh
env -u DISPLAY ./astrolog -n _X \
  -qb 11 19 1971 11:01am 0 8W 122:19:59W 47:36:35N </dev/null
```

`_X` forces text, so a settings file carrying `=X` does not open a window
and sit there; `-qb` pins the chart so runs are comparable; `</dev/null`
makes a prompt fail fast rather than look like a hang; unsetting `DISPLAY`
makes a stray graphics switch error out visibly.

It **rejects the `-W` switch family** (`case 'W':` in astrolog.cpp is
`#if defined(WIN) || defined(QT)`), and an unknown switch stops it reading
the rest of the file — so it cannot load `nrvate.as` at all. Use it for
core questions, not configuration ones.

## Driving a real window — slow, clunky, occasionally necessary

`tools/qtdrive.sh` and `tools/windrive.sh` launch the app for real and
drive it. **A run costs 60 to 240 seconds against the probe's 0.2**, and
they carry every failure mode the probe does not have. Reach for them only
when the thing under test genuinely *is* the windowing behaviour:

- a menu actually opening, and its mnemonic
- initial keyboard focus
- what a dialog looks like, for a human to review
- the Windows build, which has no other automation at all

For anything else — what a field parses, what a global holds, what a chart
draws — use the probe. It answers the same question hundreds of times
faster and cannot mis-click.

```sh
tools/qtdrive.sh tree --args "-i nrvate.as"
tools/qtdrive.sh run tools/scenarios/objectsel.txt
tools/windrive.sh run tools/scenarios/win-objectsel.txt
```

`qtdrive.sh` addresses widgets by name over AT-SPI, which is at least
better than measuring screenshots: Qt draws every widget inside one X
window, so `xwininfo` and `xdotool` see the window but not the buttons in
it, and a mis-measured click leaves the dialog open while a before/after
comparison reports "identical" because nothing happened. Scenario
commands: `menu`, `click`, `type`, `typeinto`, `expect-window`,
`expect-no-window`, `expect-value`, `shot`, `tree`, `sleep`.

Things that cost a cycle each, if you do end up here:

- **Qt only publishes its widget tree when `ScreenReaderEnabled` is true.**
  The app appears on the bus regardless, reporting zero children, which
  looks like no tree at all. `qtdrive.sh` sets it on a private bus.
- **A widget's accessible name is its current *value*, not its
  astrolog.rc symbol.** `deOs01` means nothing to AT-SPI. Buttons, menus
  and labels are reachable by name; identical rows need `role#n`, 1-based,
  hyphenated because scenario lines split on whitespace: `combo-box#5`.
- **A QComboBox is not editable through AT-SPI** — its internal line edit
  is — so `type` cannot reach one. `typeinto` clicks and uses the keyboard.
- **Wine needs a window manager too.** Not for rendering, for *input*: a
  dialog Wine has just created does not become the X focus window, so keys
  keep going to the main window and it ignores everything, Escape
  included.
- **The `expect-*` lines are the whole point.** A keystroke or click that
  lands nowhere is completely silent. Every assertion in both drivers was
  verified by making it fail on purpose.
- **`pkill -f` matches your own command line** and kills the shell running
  it, surfacing as a bare exit code 144. Use `pkill -x`, and remember
  `/proc/pid/comm` truncates at 15 characters: `astrolog-qt-tes`.

## When something stops rather than slows

**Any `PrintWarning()`/`PrintError()` becomes a modal dialog and waits
forever for a click nobody is there to give.** Three disguises: an
automated run that stops partway and never returns; the Windows build
painting its menu bar and nothing else; a chart mode that "renders slowly"
when it is not rendering at all.

`SetNoPopupQt(fTrue)` is the escape, which the captures and
`TestBadInputQt()` use. On the Windows binary it is `-Wt`. There is no
command-line escape in the Qt build.

Recognising it from outside, with no debugger:

```sh
P=$(pgrep -x astrolog-qt-tes)
cat /proc/$P/wchan                     # do_poll...
ls -l /proc/$P/fd | grep -c socket     # 0
```

Sleeping in `do_poll`, no sockets, no CPU: that is a Qt event loop waiting
on a window. It is a dialog. **`timeout`'s exit code 124 means the process
was still running, not that it was stuck.**

And time each step before theorising. Every graphics mode draws in 1–60ms
except Rising (~490ms) and the two transit grids (~275ms); each PNG saves
in 30–55ms. If something takes seconds, it is not drawing.

## Build traps

- **`Makefile.win`'s resource depends on `resource.h`.** Change control IDs
  without rebuilding the `.res` and the compiled dialog template and the
  C++ addressing it disagree — every field lands in the wrong column,
  which reads exactly like broken layout code. No audit here can see it.
- **Regenerate `qtrcdlg.h` after any `.rc` change** and rebuild.
- **Grep build output for `: error`**, not a narrower pattern.
- **Never edit a CRLF file with a range-based regex.** One in this project
  spliced two functions together by matching across the block between
  them. Exact-string replacement only, then check
  `tr -cd '\r' < f | wc -c` equals the line count. Note `sweph.cpp` ships
  from upstream with mixed endings (9 CRs in 8621 lines); not ours to fix.

## Checks worth running before a commit

```sh
make -f Makefile.qt -j4 && make -f Makefile.qt.test -j4 && ./run-qt-tests.sh
make -f Makefile.win -j4
python3 tools/rc_audit.py
python3 tools/rc_mnemonic_audit.py
python3 tools/rc2qt.py astrolog.rc | diff - qtrcdlg.h
```

## What the suite cannot tell you

It runs *inside* the program from `InteractQt()`, after the window, menus
and first chart are up. That makes it a real test of the real app, and
means two things it cannot do:

- **it cannot test startup**, because startup already happened. That check
  has to be a separate process, and is: the **Startup diagnostics**
  section of `run-qt-tests.sh`.
- **it shares live `us`/`gs`/`gi` state**, so a test that changes a
  setting must put it back.

It also says nothing about **keyboard focus**, which lives in the dialog
handlers rather than the resource, so no audit sees it either.
