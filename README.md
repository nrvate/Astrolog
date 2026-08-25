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

Nothing in the shared calculation or rendering core was changed to do
this. The new code lives in two files, `qtdriver.cpp` (main window,
canvas, menu bar) and `qtdialog.cpp` (dialogs), which stand in for the
Windows-only `wdriver.cpp`/`wdialog.cpp`. The backend is selected with
`-DQT` on the compile line, following the same pattern astrolog.h
already used for its `X11`/`WIN`/`WCLI` choice, so the existing builds
are unaffected.

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

Feature complete against the Windows build, as far as the menu bar goes.
All nine menus and every dialog are implemented, and each has been read
field-by-field against its Windows counterpart — labels, field order,
number formatting, dropdown contents, and which menu checkmarks it
refreshes on OK. Charts animate, print, paste, run all 96 macro slots,
and draw with Astrolog's bundled astrology fonts.

Not done: **right-click context menus.** Windows has one per chart type;
this build has none, so right-clicking the chart does nothing. That's the
main remaining gap and the obvious next piece of work.

A handful of smaller things are either deliberately different from
Windows or deliberately left out — mostly Win32-only settings with no
Linux meaning, and a few places where Windows' own behaviour looks like a
bug and this build does the sensible thing instead. They're all listed
under "Known divergences from Windows" in `QT_GUI_PLAN.md` rather than
left for you to discover.

## Docs

- **`QT_GUI_PLAN.md`** — read this first if you're picking the work up.
  How the Qt backend fits into Astrolog's architecture, the gotchas worth
  knowing before changing it, a "What to do next" section, per-menu
  status, a log of every item done and what it actually turned out to be,
  and every place this port knowingly differs from Windows.
- **`QT_MENU_MAPPING.md`** — the Windows menu structure extracted from
  `astrolog.rc`, with command IDs. The reference the Qt menu bar is
  built against.
