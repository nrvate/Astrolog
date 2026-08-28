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

## When the suite itself takes the process down

An assertion failure prints and carries on. A *crash* does not, and the
log then ends wherever the buffer happened to stop, which is rarely the
thing that killed it. Two of the suite's sections name each item before
they touch it and flush, so the last line printed is the culprit:

```sh
ASTROLOG_QT_TEST_VERBOSE=1 ./run-qt-tests.sh
```

That covers the 26 chart renders (`rendering: TraNatGra`) and all 338
menu items fired (`firing: Chart Settings...`). A clean run does not need
the noise, which is why it is off by default — reach for it the moment a
run dies without saying where.

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

**It loads `nrvate.as` now**, which it could not until 2026-08-27:
`case 'W':` in astrolog.cpp was `#if defined(WIN) || defined(QT)`, so this
build rejected `-WM` as an unknown switch, and an unknown switch stops
Astrolog reading the rest of the file — it gave up 240 lines in and used
defaults for everything below. So the real config, restrictions, orbs and
the `/swe` path are all available here:

```sh
env -u DISPLAY ./astrolog -i nrvate.as _X -n </dev/null | head
```

Still no GUI in it, so it answers nothing about windows, dialogs or menus
— that is what the probe and the drivers are for. What it is good for is
the calculation core and text output with the least machinery in the way,
and being an ordinary process, it takes a debugger without an event loop
in the picture.

## Driving a real window — slow, clunky, occasionally necessary

`tools/qtdrive.sh` and `tools/windrive.sh` launch the app for real and
drive it. **A run costs 60 to 240 seconds against the probe's 0.2**, and
they carry every failure mode the probe does not have. Reach for them only
when the thing under test genuinely *is* the windowing behaviour:

- a menu actually opening, and its mnemonic
- initial keyboard focus
- what a dialog looks like, for a human to review
- the Windows build, which has no other automation at all

For the Windows build that last one is packaged as `tools/win-tests.sh`,
which runs every `tools/scenarios/win-*.txt` against the real config and
reports once. It is minutes, not the suite's seconds, so it is not a
pre-commit check — run it when something ships in both builds.

For anything else — what a field parses, what a global holds, what a chart
draws — use the probe. It answers the same question hundreds of times
faster and cannot mis-click.

```sh
tools/qtdrive.sh tree --args "-i nrvate.as"
tools/qtdrive.sh run tools/scenarios/objectsel.txt
tools/windrive.sh run tools/scenarios/win-objectsel.txt
```

`qtdrive.sh` sets up the display, window manager and private D-Bus, then
runs `tools/qtdrive.py`, which is where the scenario commands and the
AT-SPI walking actually live — edit the `.py`, not the wrapper.

It addresses widgets by name over AT-SPI, which is at least
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

## One slow thing that is not a bug

A JPL Horizons lookup — the `j<n>` object definition, and Lookup Names on
such a row — takes about a second the first time. That is the service, not
this program: measured with `ASTROLOG_QT_NETLOG=1`, headers, first byte
and completion all land on the same timestamp roughly 900ms after the
request goes out, so the ~7KB reply transfers instantly and the entire
wait is Horizons computing the ephemeris before it answers.

The parts that *were* ours are done: the TLS handshake (130ms) is paid
once per session rather than once per body, and a body-and-time already
asked about is answered from cache in 0ms. Nothing else on this end
moves the number. Don't go looking for a bug in it.

Both shipped configs carry `=0n`, which disables web queries entirely, so
none of this happens unless someone asks for it.

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

**Crop the whole frame, not the corner.** A message box is centred; chart
text is top-left. A crop of the top-left corner shows a blank client area
and hides the dialog explaining it, which is how the above went unseen
through several wrong theories. Better still, measure — `non-black rows:
0..55` out of 1200 says "menu bar only" without anyone having to look.

### The same symptom, a second cause: /swe under Wine

The Windows build given `nrvate.as` paints its menu bar, leaves the client
area black and ignores every keystroke — identical to the modal dialog
above, and this time there is no dialog. The cause is `-Yi "/swe"`:
**887,000 ephemeris files**, which native Linux handles and Wine's path
translation does not. It is neither a hang nor an app bug, and `-Wt` does
nothing for it.

The tell that separates the two: screenshot the centre. A dialog is there
or it is not. Then swap the path and see it draw:

```sh
sed 's|"/swe"|"ephem"|' nrvate.as > /tmp/w.as
tools/windrive.sh run tools/scenarios/win-objectsel.txt --args "-i /tmp/w.as"
```

`tools/win-tests.sh` does that swap itself. Everything worth testing in
the config — restrictions, orbs, aspect set, macros, window size, graphics
mode — survives it; only the file count goes.

This one masqueraded as two other things first. Both were wrong and both
are worth not repeating: *"XTEST accelerators don't reach Wine"* (they do —
`ctrl+t`, `shift+alt+a` and `alt+j` all drive dialogs in
`win-dialogs.txt`), and *"graphics mode doesn't render under Wine"* (it
does; every earlier capture used `_X` for unrelated reasons, so it had
simply never been tried). Both theories came from changing two things at
once. Change one.

And time each step before theorising. Every graphics mode draws in 1–60ms
except Rising (~490ms) and the two transit grids (~275ms); each PNG saves
in 30–55ms. If something takes seconds, it is not drawing.

## Finding memory bugs: build the suite under AddressSanitizer

A fault that happens once and will not happen again is almost never a
logic error you can read your way to. ASan makes it deterministic — it
names the line, the array, and how far outside it the access went, on the
run where the damage is *done* rather than the later run where the
program falls over.

**`Makefile.qt.asan` already exists for this.** It is `Makefile.qt.test`
with `-fsanitize=address -g -O0` and its own name and object directory,
so it never clobbers the normal build:

```sh
make -f Makefile.qt.asan -j4
env -u DISPLAY QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= \
  ASAN_OPTIONS=detect_leaks=0 timeout 600 ./astrolog-qt-asan -i nrvate.as
```

`detect_leaks=0` matters: Qt does not free everything at exit and the
leak report buries any real finding. Turn it on only when leaks are what
you are hunting. Expect minutes rather than seconds — hence the
`timeout`, which is not optional courtesy: a run that goes wrong here
goes wrong by *hanging*, not by failing.

The suite is a good host because it already renders 26 chart types and
fires all 338 menu items, so one run exercises far more than a person
clicking could. `ProbeQt()` works under ASan too, which is how the
second instance of the esoteric-influence bug was confirmed rather than
merely suspected from reading. Between them, ASan has found four real
out-of-bounds bugs in this program's own chart code (plan items 24 and
37), every one of them silent in a normal build.

Two things that cost time here:

- **ASan stops at the first error**, so a fix can reveal the next one
  immediately behind it — line 1342 hid line 1456 entirely. Re-run after
  every fix until it is silent. One clean report proves nothing if the
  first error aborted the run.
- **Everything runs about ten times slower**, which loses races the
  normal build wins. The suite's modal-closing timers scale off
  `__SANITIZE_ADDRESS__` for exactly that reason; before they did, a
  dialog appeared after its own auto-close timer had fired and the run
  waited forever for a click nobody was there to give. If an ASan run
  seems endless, check it the way "When something stops rather than
  slows" says to before assuming it is just slow: 39 seconds of CPU
  against 19 minutes of wall clock is a dialog, not a sanitizer.

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

## Proving a regression test actually works

A test that passes with and without the fix is worthless, and this
project has produced two of them — each confirming an invention rather
than catching a defect. After writing one, put the bug back and watch it
fail. It costs one rebuild.

Back the files up, patch, build, run, restore:

```sh
mkdir -p /tmp/bk && cp calc.cpp extern.h /tmp/bk/
#   ... reintroduce the bug, rebuild, run, expect FAIL ...
cp /tmp/bk/calc.cpp /tmp/bk/extern.h .
```

Two ways this goes wrong, both seen here:

- **Restoring the bug is not the same as disabling the fix.** Proving the
  forced-midpoint fix meant putting back a `&&` term that had been
  negated. Writing `fFalse` there disables the skip altogether — a third
  behaviour that is neither the bug nor the fix — and the test passed
  either way. `fTrue` was the restore. Work out what the original
  expression evaluated to, not what makes the line look inert.
- **Most of these files are CRLF.** An exact-string patch written with
  bare `\n` matches nothing, and a script that asserts on the match count
  says so immediately; one that does not will happily report a pass
  against an unmodified file. Assert `count == 1` before every write.

Then state the test's preconditions rather than inheriting them. The
suite shares live `us`/`gs` state with everything that ran before it, so
a setting an earlier test left on can make a correct fix look broken —
`-4` left set put a forced midpoint exactly 30 degrees off, which reads
precisely like the fix not working.

## Checks worth running before a commit

```sh
make -f Makefile.qt -j4 && make -f Makefile.qt.test -j4 && ./run-qt-tests.sh
make -f Makefile.win -j4
python3 tools/rc_audit.py
python3 tools/rc_mnemonic_audit.py
python3 tools/rc_field_audit.py
python3 tools/rc2qt.py astrolog.rc | diff - qtrcdlg.h
python3 tools/rc_accel.py astrolog.rc | diff - qtrcaccel.h
python3 tools/rc_cmd.py astrolog.rc resource.h | diff - qtrccmd.h
```

The three `diff` lines check that the generated tables still match the
resource. Run the generators with `>` instead only when you mean to
regenerate them, and rebuild afterwards: the object files depend on those
headers, and a regeneration without a rebuild looks like it did nothing.

And when the change touches both builds, `tools/win-tests.sh` as well.
The shared logic underneath is already covered by the Qt suite, since
both builds call the same `calc.cpp` and `io.cpp`; what this adds is the
Windows dialog and menu wiring, which nothing else sees.

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
