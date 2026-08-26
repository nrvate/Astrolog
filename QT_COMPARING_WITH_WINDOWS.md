# Comparing this port against the real Windows build

Parity with Windows is this port's spec, so the most useful thing in the
repo is the ability to run the *actual* Windows binary next to the Qt one
and look. `Makefile.win` cross-compiles it with mingw-w64 from the same
`wdriver.cpp`, `wdialog.cpp` and `astrolog.rc` a Windows user runs — not
an approximation — and it runs under Wine.

This has repeatedly settled questions that reading the code got wrong.
Use it before concluding that something diverges.

## Prerequisites

```sh
sudo apt install qtbase5-dev pkg-config              # build the Qt port
sudo apt install g++-mingw-w64-x86-64 wine           # build and run the Windows one
sudo apt install xvfb metacity xdotool imagemagick   # drive it headlessly
sudo apt install python3-pil                         # compare the captures
```

Only the first line is needed to build and test the port itself. The rest
is for this comparison workflow.

## Text charts

```sh
make -f Makefile.win                        # once
make -f Makefile.qt.test                    # once

tools/text-chart-capture.sh out/win         # the Windows build, under Wine
QTTEXTDIR=out/qt ./run-qt-tests.sh          # this port
python3 tools/text-chart-diff.py out/win out/qt out/cmp
```

`out/cmp` gets one stacked image per chart type, Qt above Windows.
**Layout is what to compare** — column positions, row spacing, where each
field starts.

The two sides are captured by deliberately different means, and it matters
why. `v` is a *toggle*, so driving it leaves each build in whatever state
it happened to start in; get that wrong and a graphics chart is compared
against a text chart, which looks exactly like a rendering divergence.
The Qt side therefore sets `us.fGraphics` directly in code
(`TextChartCaptureQt()` in `qttest.cpp`), needing no display, no window
manager and no keystrokes. The Wine side gets the same determinism by
launching as `wine ./astrolog.exe _X`, which clears `us.fGraphics` at
startup and leaves the GUI up in text mode.

Both sides pin the same chart — Nov 19 1971 11:01am, ST Zone 8W, 122:19W
47:36N, with no name or location string. Change it in both or the
comparison is only about layout, not values.

## Reading the result

Two things look like divergences and are not:

- **The header gains a second line whenever a chart *name* is set.**
  `charts1.cpp:91` emits a newline after the name. If one side has a name
  and the other doesn't, every row below shifts by one.
- **Cusp values differ if the two sides disagree about 3D houses.** The
  header says `3D Placidus Houses` rather than `Placidus Houses` when
  `us.fHouse3D` is on. That is a setting, not a renderer difference.

As of 2026-08-25 all eight text chart types are character-for-character
identical in layout, including the IBM box-drawing path that `=k` enables.

Colour counts in the diff output are a coarse mode check, not a quality
measure: Astrolog draws from a small fixed palette with no antialiasing,
so a Windows text capture lands around 6 colours and the antialiased Qt
one around 190. Wildly different counts *for the same chart* usually mean
the two sides ended up in different modes. Don't read a low count as
"blank" on its own — crop out the window chrome and open the image. A
full-window capture includes an antialiased menu bar, which puts hundreds
of colours into even a pure graphics chart and destroys the signal.

## Driving either build headlessly

```sh
Xvfb :77 -screen 0 1200x900x24 &
DISPLAY=:77 metacity --sm-disable &
DISPLAY=:77 wine ./astrolog.exe &        # or ./astrolog-qt
```

On a private Xvfb display `import -window root` is fine and much easier
than chasing window IDs. On a real display it is forbidden — see
`CLAUDE.md`.

Traps, each of which cost real time here:

- **Qt needs a window manager for menus to open.** Under bare Xvfb the Qt
  app runs and renders fine, but Alt+mnemonic and menu-bar clicks silently
  do nothing and no popup window is ever mapped. Wine manages its own
  windows and doesn't need one, which makes the failure look
  app-specific rather than environmental.
- **`xdotool key --window <id>` uses XSendEvent, which Wine ignores.**
  Keys appear delivered and nothing happens; captures then show the
  *previous* chart, which reads as redraw lag rather than as input never
  arriving. Activate the window and send via XTEST — plain `xdotool key`,
  no `--window`.
- **Astrolog's accelerators are case-sensitive.** `v` is Show Graphics;
  `V` (`shift+v`) is Standard Radix. `Alt+l` and `Alt+L` are different
  commands. Send `shift+a`, not `a`.
- **Wine under Xvfb doesn't reliably repaint between commands.** A plain
  sleep isn't enough; resizing the window away and back forces a real
  expose.
- **Xvfb isolates the display, not audio.** Astrolog `MessageBeep`s on
  every keystroke it doesn't handle, so a capture run plays sound on the
  real speakers from a program nobody can see. `MessageBeep` is in
  `user32` and reaches the audio backend even with `winmm` disabled —
  disable the wine audio *drivers* (`winepulse.drv=d;winealsa.drv=d;...`),
  which is what `tools/text-chart-capture.sh` does.
- **Don't `pkill -f` a pattern that matches your own command line.**
  `pkill -f astrolog-qt` kills the shell running it, which surfaces as a
  bare exit code 144 and no output — easy to misread as the app crashing.
  Use `pkill -x <exact-name>`, or match on a PID. Wine also leaves a
  `wineserver` behind that outlives the app; `wineserver -k` clears it.

## Menus and dialogs

For menu structure, don't compare screenshots — compare against the
resource script, which is the thing both builds are built from:

```sh
python3 tools/rc_mnemonic_audit.py    # "&" placement, all 848 label sites
python3 tools/rc_audit.py             # dialog controls nothing wires up
python3 tools/rc2qt.py astrolog.rc > qtrcdlg.h   # regenerate dialog tables
```

`rc2qt.py` reproduces the committed `qtrcdlg.h` byte-for-byte, so a diff
after regenerating means upstream's resource changed.

The test suite also asserts menu parity directly: 257 of 257 Windows menu
items present, checked against `astrolog.rc` at runtime.
