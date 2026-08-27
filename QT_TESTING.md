# Seeing what this app actually does

How to run, drive and observe the Qt build without a display, and the
failure modes that waste hours if you don't know them. Written after a
session that hit most of them.

`QT_COMPARING_WITH_WINDOWS.md` covers diffing this build against the real
Windows one. This file is about observing *this* build.

## The short version

```sh
make -f Makefile.qt.test -j4

./run-qt-tests.sh                       # 2773 assertions, ~22 seconds
QTTEXTDIR=out/qt  ./run-qt-tests.sh     # 8 text charts   -> PNG, ~2 seconds
QTGRAPHDIR=out/qtg ./run-qt-tests.sh    # 24 graphics charts -> PNG, ~3 seconds
```

The two capture forms **render and exit without running the assertion
suite** (the wrapper still runs its short Startup diagnostics section
afterwards, which is a second or two). No X display, no window manager, no `xdotool`. This is the fastest
way to look at what the program draws, and it is almost always the right
tool — reach for a real window only when the thing under test is genuinely
interactive (a dialog, focus, a menu).

They live in `qttest.cpp` as `TextChartCaptureQt()` and
`GraphicsChartCaptureQt()`; both pin the same chart (Nov 19 1971 11:01am,
ST Zone 8W, Seattle to the arcsecond) so the two sets describe the same
data. Charts are written at whatever `gs.xWin`/`gs.yWin` currently are.

Adding a mode is one line in each of the two arrays at the top of
`GraphicsChartCaptureQt()`. `gAspect` and `gArabic` are deliberately
absent: `DrawChartX()` has no case for either, which is why Windows forces
text mode for exactly those two.

## The one that will get you: a modal warning stops everything

**Any `PrintWarning()`/`PrintError()` becomes a modal dialog and waits
forever for a click nobody is there to give.** This is the single most
expensive failure mode in this codebase and it has three different
disguises:

- an automated capture that stops partway through and never returns;
- the Windows build under Wine painting its menu bar and nothing else
  (`QT_COMPARING_WITH_WINDOWS.md` has that story);
- a chart mode that "renders slowly" when it is in fact not rendering.

**The escape is `SetNoPopupQt(fTrue)`** around anything unattended, which
`TestBadInputQt()` and both capture functions do. On the Windows binary
the equivalent is launching with `-Wt`. Note there is *no* command-line
escape in the Qt build: `-Wt` sets Win32-only `wi.fNoPopup`, and nothing
on the Qt command line reaches `SetNoPopupQt()`.

**How to recognise it from outside**, without a debugger:

```sh
P=$(pgrep -x astrolog-qt-tes)          # note: 15-char truncation, see below
cat /proc/$P/status | grep State       # S (sleeping)
cat /proc/$P/wchan                     # do_poll...
ls -l /proc/$P/fd | grep -c socket     # 0
```

Sleeping in `do_poll`, no sockets, no CPU: that is a Qt event loop waiting
on a window. It is a dialog. It is not slow, it is not the network, and it
is not the ephemeris.

A worked example: `gMoons` wants moon ephemeris files that aren't in
`ephem/`, warns, and hung the graphics capture at chart 15 of 24 —
which read as "the capture is slow" for far longer than it should have.

## Distinguish "slow" from "stopped" before theorising

Time each step. It is two lines and it ends the argument:

```c
QElapsedTimer tim;
tim.start();
SetChartModeQt(rgnMode[i]);
printf("  %-12s draw %lldms\n", rgszFile[i], (long long)tim.elapsed());
fflush(stdout);
```

For reference, on this machine every graphics mode draws in **1–60ms**
except Rising (~490ms) and the two transit grids (~275ms), and each PNG
saves in 30–55ms. If something takes seconds, it is not drawing.

**`timeout`'s exit code 124 means the process was still running, not that
it was stuck.** Check what it was doing before calling it a hang.

## The console build is the X11 build

`make` (the plain Makefile) links `-lX11`. So:

- a settings file containing `=X` (graphics mode) makes it **open a chart
  window and wait** — that is the program working, not a hang. With
  `DISPLAY` unset it says "Can't open display" and exits 1;
- force text with `_X` and it prints a chart and exits 0;
- it **rejects the `-W` switch family** as unknown, because `case 'W':` in
  astrolog.cpp is guarded `#if defined(WIN) || defined(QT)`. An unknown
  switch stops it reading the rest of the file, so a settings file written
  by the Qt or Windows build stops being readable at its first `-WM` line.

That last point means the console build is **not** a good way to test a
real user's config. Use the Qt capture instead.

What it *is* good for is a fast smoke test of the shared calculation and
text-rendering core, with no display and no GUI in the way. This exits 0
and prints a chart:

```sh
env -u DISPLAY ./astrolog -n _X \
  -qb 11 19 1971 11:01am 0 8W 122:19:59W 47:36:35N </dev/null
```

`_X` forces text, `-qb` pins the chart so successive runs are comparable,
`</dev/null` means a prompt fails fast instead of waiting, and unsetting
`DISPLAY` means a stray graphics switch errors out visibly rather than
opening a window. Strip the ANSI colour with
`sed 's/\x1b\[[0-9;]*m//g'` if you want to diff two runs.

Astrolog's own switch reference is `./astrolog -H`, and the rare ones are
`-Y` — quicker than reading `charts0.cpp` when you need the exact
argument order for something like `-qb`.

## Driving dialogs by widget name, not by pixel

`tools/qtdrive.sh` removes the worst kludge in this repo. Qt draws all its
widgets inside one X window, so `xwininfo` and `xdotool` can see the window
but not the buttons in it — which is why driving a dialog by hand means
measuring a screenshot, and why a mis-measured click leaves the dialog open
while a before/after comparison cheerfully reports "identical". Nothing
happened, and nothing said so. That exact false pass cost real time.

Qt publishes every widget over AT-SPI, so a button can be found by its
label and clicked where it says it is:

```sh
tools/qtdrive.sh tree                          # dump the whole widget tree
tools/qtdrive.sh tree --args "-i nrvate.as"    # ... with the app given args
tools/qtdrive.sh run tools/scenarios/objectsel.txt
```

A scenario is one command per line:

```
menu Setting Object Selectio&ns...    open a menu bar item, then an entry
click OK                              click a widget by label
type combo-box#3 Nessus               set a field
expect-window Object Selections       fail unless that window exists
expect-no-window Object Selections    fail unless it does not
expect-value combo-box#1 Vulcan       fail unless the field reads that
shot out/drive/x.png                  screenshot
tree                                  dump the tree at this point
```

Exit status is 0 only if every `expect-*` passed. **Those assertions have
teeth** — verified by making each one fail on purpose: leaving the dialog
open fails `expect-no-window`, and a wrong value fails `expect-value` with
what it actually found.

The wrapper gives the run a private Xvfb display, a private D-Bus session
and its own metacity, so it touches neither your screen nor your desktop's
accessibility settings.

Four things that are not obvious, all of which cost a cycle here:

- **Qt only publishes the tree when `ScreenReaderEnabled` is true.** The
  app appears on the bus regardless, reporting zero children, which looks
  like the tree not existing. `qtdrive.sh` sets it on its private bus.
- **A widget's accessible name is its *value*, not its resource symbol.**
  The dialogs are generated from `astrolog.rc`, but `deOs01` means nothing
  to AT-SPI; that field reports whatever it currently displays. Buttons,
  menus and labels are reachable by name; a grid of identical rows needs
  `role#n`, 1-based, as in `combo-box#5`.
- **Roles contain spaces**, and scenario lines are split on whitespace, so
  selectors are written with hyphens: `combo-box`, `push-button`,
  `check-box`.
- **Menu bar entries have role `menu item`**, the same as the entries
  inside them — the role is not what distinguishes a menu from its items.

The menu commands go through the accessible action rather than
`alt+mnemonic`, so the "second key sent too early does nothing" flake
cannot happen.

**It is Qt-only.** The Windows build under Wine has no AT-SPI, so that
still needs the coordinate method below.

A harmless `A connection to the bus can't be made` on exit is the private
bus being torn down after the app has already gone.

## Driving a real window, when you actually need one

For dialogs, focus and menus there is no substitute. Use a private Xvfb
display, never the user's own (see `CLAUDE.md` on screenshots).

```sh
Xvfb :83 -screen 0 1280x1024x24 &
PULSE_SERVER=/nonexistent DISPLAY=:83 metacity --sm-disable &   # Qt needs a WM for menus
DISPLAY=:83 ./astrolog-qt -i somefile.as &
```

`PULSE_SERVER=/nonexistent` matters: metacity plays the X bell through the
real speakers, and Astrolog rings it on every keystroke it doesn't handle.

Then, in order of how much time each has cost:

- **Use `xwininfo` for coordinates, not `xdotool getwindowgeometry`.** The
  latter does not report the client origin, and clicks land in the wrong
  place:
  ```sh
  eval $(DISPLAY=:83 xwininfo -id $W | awk '/Absolute upper-left X/{print "DX="$4} /Absolute upper-left Y/{print "DY="$4}')
  ```
- **Verify the click landed.** A missed OK leaves the dialog open, and a
  before/after comparison then shows "identical" because *nothing
  happened*. Always check the window is gone:
  ```sh
  DISPLAY=:83 xdotool search --onlyvisible --name 'Object Selections' >/dev/null && echo STILL OPEN
  ```
- **Give menus time.** `alt+s` then the mnemonic works, but sending the
  second key too early silently does nothing. Screenshot the menu to
  confirm it opened before picking from it.
- **Accelerators are case-sensitive** — `v` and `V` are different commands.
- **`pkill -f <pattern>` matches your own command line** and kills the
  shell running it, which surfaces as a bare exit code **144**. Use
  `pkill -x`, and remember `/proc/pid/comm` truncates at 15 characters, so
  the exact name for the test binary is `astrolog-qt-tes`.

## Driving the Windows build

`tools/windrive.sh` is the same idea for the real Windows binary under
Wine, with the same scenario vocabulary:

```sh
make -f Makefile.win
tools/windrive.sh run tools/scenarios/win-objectsel.txt
tools/windrive.sh shell                      # leave it up on a private display
```

Wine has no AT-SPI, so there is no addressing a widget by name over there —
it is keys and titles. What the wrapper removes is the setup, every part of
which went wrong by hand at least once: `-Wt` so a modal `PrintWarning`
cannot hang the run, the `WINEDLLOVERRIDES` that actually silence
`MessageBeep`, `wineserver -k` on the way out because it outlives the app,
and waiting for the window to map rather than guessing a sleep.

**Wine needs a window manager too.** Not for rendering — it draws fine
without one, which is why `tools/text-chart-capture.sh` runs without one —
but for *input*. A dialog Wine has just created does not become the X focus
window on its own, so keystrokes keep going to the main window and the
dialog appears to ignore everything, Escape included. This cost a cycle
here and the older note in `QT_COMPARING_WITH_WINDOWS.md` said the
opposite; it is corrected now. Even with a window manager, focus a dialog
explicitly by title before sending it keys:

```
menu s n
expect-window Object Selections
focus Object Selections
key Escape
expect-no-window Object Selections
```

**The `expect-*` lines are the whole point.** A keystroke that lands
nowhere is completely silent, so a Wine scenario without them proves
nothing at all. Both drivers' assertions were checked by making them fail
on purpose.

## Build traps

- **`Makefile.win`'s resource depends on `resource.h`** (it didn't, until
  this was hit). Change control IDs without rebuilding the `.res` and the
  compiled dialog template and the C++ addressing it disagree — every
  field lands in the wrong column, which reads exactly like broken layout
  code. No audit here can see it: they read the `.rc` and the header as
  text, not what got linked.
- **Regenerate `qtrcdlg.h` after any `.rc` change** and rebuild; the
  makefiles list it as a dependency, but a stale object file still makes a
  regeneration look like it did nothing.
- **Grep build output for `: error`**, not a narrower pattern.

## If you extend the drivers

Two things bit while writing them, and will bite again:

- **Don't `exec` past a cleanup trap.** `qtdrive.sh` originally `exec`'d
  into `dbus-run-session`, which replaces the shell and takes its `trap`
  with it — so every run leaked an Xvfb and a metacity, silently, until a
  process check caught it. Run it as a child and clean up after.
- **A scenario with no `expect-*` line proves nothing.** Keys and clicks
  that land nowhere are completely silent on both platforms. Every
  assertion in both drivers was checked by making it fail on purpose,
  which is the only way to know an assertion works; this repo has shipped
  tests before that confirmed an invention rather than caught a defect.

## Checks worth running before a commit

```sh
make -f Makefile.qt -j4 && make -f Makefile.qt.test -j4 && ./run-qt-tests.sh
make -f Makefile.win -j4          # the Windows build must keep compiling
python3 tools/rc_audit.py         # controls nothing wires up
python3 tools/rc_mnemonic_audit.py
python3 tools/rc2qt.py astrolog.rc | diff - qtrcdlg.h
```

And for any upstream (CRLF) file you touched:

```sh
tr -cd '\r' < file | wc -c        # must equal the line count
```

Note `sweph.cpp` ships from upstream with mixed endings (9 CRs in 8621
lines). That is not something this fork did; don't "fix" it.

## What the suite can and cannot tell you

It runs *inside* the program from `InteractQt()`, after the window, menus
and first chart are up. That makes it a real test of the real app, and it
means two things it cannot do:

- **it cannot test startup**, because startup already happened. `main()`
  parses `astrolog.as` and the command line long before the QApplication
  exists, and a warning raised there once dumped core. That check has to
  be a separate process, and is: the **Startup diagnostics** section of
  `run-qt-tests.sh`.
- **it shares live `us`/`gs`/`gi` state**, so a test that changes a setting
  must put it back.

It also says nothing about **keyboard focus**, which lives in the dialog
handlers rather than the resource, so no audit sees it either. Seven
dialogs set focus explicitly on Windows; that gap was found by a user
typing into a box, not by a test.
