# Astrolog 8.00 — with a Qt GUI for Linux

This is a fork of Astrolog 8.00 (released May 31, 2026), described at
http://www.astrolog.org/astrolog.htm and originally copied from
http://www.astrolog.org/ftp/ast80src.zip — the changes upstream made in
8.00 are listed at http://www.astrolog.org/ftp/updat800.htm

Everything Astrolog does, and all of its chart calculation and drawing
code, is Walter D. Pullen's work. See LICENSE.HTM.

## What this fork adds

A **Qt GUI backend for Linux**, with the menus and dialogs the Windows
build has. Upstream's Linux build uses X11 directly and is driven by
single-keystroke commands; this one gives you the same nine menus and
~28 dialogs a Windows user would recognise, so the two builds can be
used more or less interchangeably.

The port itself changes almost nothing in the shared calculation or
rendering core, and where it must, it does so inside `#ifdef QT`. The new
code lives in two files, `qtdriver.cpp` (main window, canvas, menu bar)
and `qtdialog.cpp` (dialogs), which stand in for the Windows-only
`wdriver.cpp`/`wdialog.cpp`. The backend is selected with `-DQT` on the
compile line, following the same pattern astrolog.h already used for its
`X11`/`WIN`/`WCLI` choice, so the existing builds are unaffected.

Separately, the fork adds a few things to **both** builds — a new Object
Selections dialog, and fixes for settings the program was silently
failing to save. Those deliberately carry no `QT` guard, because they go
into `astrolog.rc`, `wdialog.cpp` and the shared code the way upstream
would take them. See "Features this fork adds to both builds" in
`QT_GUI_PLAN.md`.

## Building

```
make -f Makefile.qt
./astrolog-qt
```

Needs the Qt5 development packages (`qtbase5-dev` on Debian/Ubuntu/Mint)
and `pkg-config`. Object files go to `obj-qt/`, so this can be built
alongside the regular `astrolog` binary without interfering with it —
`make` still builds the stock X11 version.

## Status

Feature complete against the Windows build. All nine menus, all 258 menu
items, every dialog, all 42 right-click context menus and 264 keyboard
shortcuts are implemented, and each dialog has been read field-by-field
against its Windows counterpart — labels, field order, number formatting,
dropdown contents, and which menu checkmarks it refreshes on OK. Menu
mnemonics sit on the same letters Windows uses, so Alt-key muscle memory
carries over. Charts animate, print, paste, run all 96 macro slots, and
draw with Astrolog's bundled astrology fonts. Text charts render in the
main window on a character grid, as they do on Windows, rather than in a
separate text box.

It also goes slightly *past* the Windows build in one place: an Object
Selections dialog (Ctrl+T) that puts a chosen body or a midpoint into any
Uranian or Dwarf slot, which neither build previously offered — added to
both, not just this one.

A handful of smaller things are either deliberately different from
Windows or deliberately left out — mostly Win32-only settings with no
Linux meaning, and a few places where Windows' own behaviour looks like a
bug and this build does the sensible thing instead. They're all listed
under "Known divergences from Windows" in `QT_GUI_PLAN.md` rather than
left for you to discover.

## Appearance

The Qt build follows the desktop's light/dark setting. Qt 5 has no API for
this — `QStyleHints::colorScheme()` arrived in Qt 6.5 — and Qt 5's own gtk3
platform theme loads without supplying a palette, so Astrolog reads the
preference itself: the `org.freedesktop.appearance` portal first, which is
what Qt 6.5 reads too, then GNOME/Cinnamon/MATE via `gsettings`, XFCE via
`xfconf-query`, then `kdeglobals`, `gtk-3.0/settings.ini` and `GTK_THEME`.
The last three need no helper programs installed.

Set `ASTROLOG_QT_THEME=dark` or `=light` to override the detection:

```sh
ASTROLOG_QT_THEME=light ./astrolog-qt
```

## Tests

```
make -f Makefile.qt.test
./run-qt-tests.sh
```

A headless suite of 3018 assertions plus startup checks, covering dialogs, context menus,
shortcuts, chart rendering, every menu item, menu parity against
`astrolog.rc`, and bad input. Several groups drive the real dialogs and
assert what they leave behind, rather than calling the code underneath
them. No X display needed; exits nonzero on failure.

It defaults to `-i nrvate.as`, the maintainer's settings file, because
that is the only input under which the esoteric bodies resolve at all —
without it a fifth of the checks skip themselves silently.

The Windows build has a small suite of its own, driving the real binary
under Wine:

```
tools/win-tests.sh
```

It takes minutes rather than seconds, so it is run when a change ships in
both builds rather than before every commit.

There are also three audits that check this port against Windows'
resource script directly:

```
python3 tools/rc2qt.py astrolog.rc > qtrcdlg.h   # regenerate dialog tables
python3 tools/rc_audit.py                        # controls nothing wires up
python3 tools/rc_mnemonic_audit.py               # "&" placement vs the .rc
```

## Comparing against the real Windows build

`Makefile.win` cross-compiles the actual Windows binary with mingw-w64 —
same `wdriver.cpp`, same `wdialog.cpp`, same `astrolog.rc` — so it can be
run under Wine and compared side by side rather than reasoned about:

```
make -f Makefile.win
wine ./astrolog.exe
```

There's a scripted comparison too, which captures the same text charts
from both builds and stacks them for a look:

```
tools/text-chart-capture.sh out/win
QTTEXTDIR=out/qt ./run-qt-tests.sh
python3 tools/text-chart-diff.py out/win out/qt out/cmp
```

Needs `g++-mingw-w64-x86-64`, `wine`, `xvfb`, `metacity`, `xdotool`,
`imagemagick` and `python3-pil`. See `QT_COMPARING_WITH_WINDOWS.md`.

## Docs

- **`CLAUDE.md`** — orientation for picking the work up from a fresh
  clone: build and test commands, the audits, the Wine reference build,
  and the rules this repo is worked under.
- **`QT_GUI_PLAN.md`** — read this first if you're picking the work up.
  How the Qt backend fits into Astrolog's architecture, the gotchas worth
  knowing before changing it, a "What to do next" section, per-menu
  status, a log of every item done and what it actually turned out to be,
  and every place this port knowingly differs from Windows.
- **`QT_TESTING.md`** — how to answer a question about the program in
  about a fifth of a second, render any chart to a PNG with no display,
  and the traps that waste hours if you drive a window instead.
- **`QT_COMPARING_WITH_WINDOWS.md`** — how to build the real Windows
  binary, run it under Wine, and diff it against this port.
- **`QT_MENU_MAPPING.md`** — the Windows menu structure extracted from
  `astrolog.rc`, with command IDs. The reference the Qt menu bar is
  built against.
