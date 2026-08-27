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

### Why the Windows build is launched with `-Wt`

`SwissEnsurePath()` (calc.cpp) builds the Swiss Ephemeris search path by
concatenating the executable's directory, the working directory, several
environment variables and a compile-time directory into a single
`char[AS_MAXCH]` — 256 bytes. Run from a deep enough working directory
and that overflows, at which point Astrolog calls `PrintWarning()`, which
on Windows is a **modal** `MessageBox`, put up before the first chart is
ever drawn.

The app then sits on that dialog. The menu bar is painted, the client area
stays blank, and every keystroke goes to the dialog instead of the chart —
so all eight captures come out identical and empty. It looks exactly like
a broken checkout or a rendering divergence, and it is neither. The
threshold here was between a 50-character working directory (fine) and a
70-character one (blank); a checkout under a temp directory clears it
easily.

`-Wt` sets `wi.fNoPopup`, which makes `PrintWarning()` return instead of
putting up the box. The script passes it, and also refuses to hand back a
capture directory whose images are all byte-identical rather than let a
silent failure through.

**The lesson that cost the most time here: look at the whole frame.** The
dialog was in every single capture from the start. It went unseen for an
hour because every crop was of the top-left corner, where the chart text
belongs — and a `MessageBox` is centred. When something renders blank,
open the full image before theorising about why.

The two sides are captured by deliberately different means, and it matters
why. `v` is a *toggle*, so driving it leaves each build in whatever state
it happened to start in; get that wrong and a graphics chart is compared
against a text chart, which looks exactly like a rendering divergence.
The Qt side therefore sets `us.fGraphics` directly in code
(`TextChartCaptureQt()` in `qttest.cpp`), needing no display, no window
manager and no keystrokes. The Wine side gets the same determinism by
launching as `wine ./astrolog.exe _X`, which clears `us.fGraphics` at
startup and leaves the GUI up in text mode.

Both sides pin the same chart — Nov 19 1971 11:01am, ST Zone 8W, at the
exact location `astrolog.as` carries (`-zl 122W19'59 47N36'35`), with no
name or location string. Change it in both or the comparison is only
about layout, not values.

**Use the location to the second.** The header only ever displays whole
minutes, so rounding to `122:19W 47:36N` looks right and matches the
header — while shifting every house cusp by one to two arcminutes. The
planets stay correct, which makes it read as a calculation divergence
between the two builds rather than as bad test data. With the exact
values the two sides are character-for-character identical.

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
Reproduced 2026-08-26 from a fresh clone on a second machine (Wine 9.0),
so the result is the builds' and not one checkout's.

Colour counts in the diff output are a coarse mode check, not a quality
measure, and **the two sides are not comparable to each other** — the
counts only mean anything against the same side's previous runs.

The reason is that the two are captured at different extents. The Qt side
writes `gi.qim` straight to a PNG, so it is the bare chart: measured
2026-08-26, 181-192 colours across the eight types. The Windows side is
`import -window root`, the whole 1200x900 display, so it also carries
Wine's antialiased menu bar, scrollbars and window frame: 1468-2245 for
the identical charts. A `192` beside a `2121` is the normal reading, not
a divergence.

Wildly different counts *for the same chart on the same side* usually mean
that side ended up in a different mode. Don't read a low count as "blank"
on its own — open the image. And don't add chrome to the Qt side to make
the numbers line up; the asymmetry is what keeps the Qt figure sensitive
to the chart itself.

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

- **Both builds need a window manager, for different reasons.** Under bare
  Xvfb the Qt app runs and renders fine, but Alt+mnemonic and menu-bar
  clicks silently do nothing and no popup is ever mapped. *(Corrected
  2026-08-26: this used to say Wine "manages its own windows and doesn't
  need one". That is true of rendering and false of input. A dialog Wine
  creates does not become the X focus window on its own, so keystrokes keep
  going to the main window and the dialog appears to ignore everything --
  including Escape. Start metacity for Wine too, as
  `tools/windrive.sh` does.)*
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
- **Xvfb isolates the display, not the session's sound server.** A
  headless run can make audible noise on the real speakers, from a window
  nobody can see. There are two separate sources and they need separate
  fixes:
  - **metacity plays the X bell**, as the desktop sound theme's
    `bell-window-system` sample, through the user's PulseAudio. Astrolog
    rings the bell on every keystroke it doesn't handle, so a capture run
    is a burst of them. This is the loud one and it is easy to misattribute
    to Wine. `tools/text-chart-capture.sh` starts **no** window manager,
    because Wine doesn't need one. Qt does — its menus silently never open
    without one — so when running Qt, start it as
    `PULSE_SERVER=/nonexistent metacity --sm-disable &`, which leaves
    libcanberra unable to reach the sound server and touches no user
    setting. Note `xset -b` alone is *not* enough: metacity handles the
    bell itself.
  - **Wine's `MessageBeep`** is in `user32` and reaches the audio backend
    even with `winmm` disabled, so disable the wine audio *drivers*
    (`winepulse.drv=d;winealsa.drv=d;...`), which the script does.

  To identify a mystery sound rather than guess at it, watch PulseAudio
  event-driven — `pactl subscribe`, dumping `pactl list sink-inputs` on
  each `sink-input` event. Polling on a timer misses these entirely; they
  last a fraction of a second. The stream names its own source
  (`application.name = "Metacity"`).
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
