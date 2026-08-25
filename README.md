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

All nine menus and their dialogs are implemented. Every settings dialog
has been checked field-by-field against its Windows counterpart — labels,
field order, number formatting, dropdown contents, and which menu
checkmarks it refreshes on OK. The chart list, multi-chart, command line,
and About dialogs exist and work but haven't had that same line-by-line
pass.

Still missing: the animation loop (the Animate menu sets its state, but
nothing drives it yet), Edit > Paste, the 96 macro slots, and
File > Print.

## Docs

- **`QT_GUI_PLAN.md`** — how the Qt backend fits into Astrolog's
  architecture, the gotchas worth knowing before changing it, per-menu
  status, remaining work, and every place this port knowingly differs
  from Windows. Read this first if you're picking the work up.
- **`QT_MENU_MAPPING.md`** — the Windows menu structure extracted from
  `astrolog.rc`, with command IDs. The reference the Qt menu bar is
  built against.
