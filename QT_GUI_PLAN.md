# Qt GUI parity plan

Tracks progress toward full menu/dialog parity between Windows Astrolog
(`wdriver.cpp`/`wdialog.cpp`) and this fork's Qt Linux GUI backend
(`qtdriver.cpp`/`qtdialog.cpp`/`qtdriver.h`, branch `qt`, built via
`Makefile.qt` with `-DQT`). Read this alongside `QT_MENU_MAPPING.md` (the
extracted Windows menu structure, with command IDs) — together they're the
full picture. This doc exists so a future session (or a fresh agent with
no memory of prior sessions) can pick up exactly where things left off
without re-deriving any of this.

**Before touching anything**, re-verify this doc's "done" claims against
the actual current `qtdriver.cpp`/`qtdialog.cpp` (`grep -n "^static void
Build" qtdriver.cpp` to find each menu-builder function, `grep -n "^void
Show" qtdialog.cpp` for the dialog list) — this snapshot is accurate as of
2026-08-25, but don't trust it blindly if it's been a while.

**Update 2026-08-26 — using it found what testing it had not.** The
port was run against its maintainer's own long-standing `astrolog.as`
rather than the repo's default, and inside an hour that turned up a
startup core dump (item 27), initial focus on the wrong control in seven
dialogs (item 28), and a leak warning on every exit (item 29). Then
saving settings turned out to drop seven switch families silently, and
then the new Object Selections dialog turned out to reject three of the
four ways a user would name a body. All fixed. **Not one was reachable
by the resource audits or the in-process suite**, and none needed a
comparison against Windows — only somebody using it normally.

**Two things follow, and they are the most useful sentences on this
page.** First: drive real charts with real settings, always with
`-i nrvate.as`, rather than adding coverage to what is already measured.
Second: ask the program directly rather than driving its window —
`ProbeQt()` in qttest.cpp answers a question in about 0.2 seconds with no
display at all, and a session was lost to not knowing that (items 34 and
QT_TESTING.md). Item 30 is the one thing that session raised which turned
out to be a setting working correctly, kept because the wrong theory
about it is worth not repeating.

**Where things stand (2026-08-26). Every item on this plan is done**, and
the fork has since gone slightly past parity: see "Features this fork
adds to both builds".
All nine menus are built and all ~28 dialogs exist and have been read
field-by-field against their `Dlg*` in wdialog.cpp and their resource
block in astrolog.rc — the item-8 sweep covered the settings dialogs,
item 14 covered the four outside it. The Qt build now also animates,
prints, pastes, runs all 96 macros, and renders the bundled astrology
fonts. Items 1-15 below are the complete record, each one saying what was
found and what was deliberately left.

**So there is no queue to pick up from.** If you are here to do work,
read "What to do next" immediately below and pick something; don't invent
a task by re-reading the item list, and don't assume something is missing
because it isn't mentioned — everything knowingly skipped or deliberately
diverged is in the relevant item or in "Known divergences from Windows"
near the end. Something undocumented is a doc bug worth fixing, not a
decision someone made silently.

## What to do next

Roughly in the order I'd take them.

1. ~~Right-click context menus~~ — **done 2026-08-25, all 42.** Right
   clicking brings up the menu for the current chart, dispatched the way
   Windows' `DoPopup()` is from `WM_RBUTTONDOWN` (wdriver.cpp ~line 938):
   the 20 graphics menus off the canvas, switched on `gi.nMode`, and the
   22 text ones off the text chart window, chosen from the `us.f*` flags
   by an else-if chain whose order matters and is kept as Windows has it.
   - Each menu is a `CTXITEM` table of `{label shown here, label of the
     menu bar item to act through}` with `{NULL, NULL}` for a separator.
     All 42 were generated from astrolog.rc rather than transcribed,
     cross-referencing each `cmd*` against the main menu block in the
     same file. Worth reusing that approach for anything similar.
   - Entries proxy to the menu bar's action rather than reusing it,
     because Windows gives one command different labels per context
     (`cmdChartModify` is "Draw Houses Same Size" on a Western wheel and
     "Toggle North Indian" on an Indian one). The proxy mirrors the real
     action's checkmark and is rebuilt on each right-click, so nothing is
     reimplemented and there's still one piece of state per command.
   - An entry whose target doesn't resolve shows greyed rather than being
     dropped, which caught three real label differences between Windows
     and this port during the work.
   - Text charts show in their own `QTextBrowser` window, so those menus
     replace its default copy/select-all menu via `CustomContextMenu`.

2. ~~Keyboard shortcuts~~ — **done 2026-08-25.** Astrolog's whole
   single-keystroke interface lives in the `accelerator ACCELERATORS`
   table in astrolog.rc (~line 3061) and none of it existed here; the
   only keys that did anything were the menu mnemonics and the 96 macro
   F-keys. All but 23 of the non-macro accelerators are now bound (the
   exact totals drift as entries are added; the 23 are enumerated below).
   - Bound onto the menu bar's existing `QAction`s by label, the same
     technique the context menus use, so nothing is reimplemented. Qt
     then draws the shortcut beside each menu item, which Windows does
     and this port previously didn't — a parity gap closed for free.
   - Generated from the resource, with Windows' `VK_OEM_*` virtual keys
     mapped to their US-layout characters (`OEM_PLUS` is `=`, `OEM_4` is
     `[`, and so on). Three commands needed a hand-mapped label where
     this port's wording differs: Exit/Quit, Save Chart Info, Open
     Documentation.
   - **23 accelerators are deliberately unbound**, and all of them belong
     to command groups already listed as out of scope: the Setup submenu,
     the Window Settings submenu, Print Setup, and the wallpaper modes —
     plus the four text-scrolling ones, which the text window's own
     scrollbar handles.
   - **Shortcuts had to be added to the text chart window too.** A Qt
     shortcut fires only for the active window, and text charts live in
     their own window, so without `AddHotkeysToWindowQt()` every hotkey
     went dead the moment text mode opened.
   - Verified: `v` toggles graphics/text both ways and from either
     window, and Alt+Shift+N switches to the transit-and-natal chart.

3. ~~A regression check~~ — **done 2026-08-25.** `make -f
   Makefile.qt.test && ./run-qt-tests.sh`. Runs headless in seconds, no X
   display and no `xdotool`, and exits non-zero on failure. **2777
   assertions** as of 2026-08-25; it was 1396 when first written, and has
   since grown to cover menu parity against `astrolog.rc` (258/258), the
   Chart menu's graphics/text handling, and bad input.
   - **How it works.** `Makefile.qt.test` builds the same sources plus
     `qttest.cpp` with `-DQTTEST` into `astrolog-qt-test`, in its own
     object directory, so the shipped binary carries no test code.
     `InteractQt()` runs the suite instead of `exec()` when that's
     defined, which means tests see the real app after menus, hotkeys and
     the first chart are all up — not a fixture. Headless comes from
     `QT_QPA_PLATFORM=offscreen`; `QT_QPA_PLATFORMTHEME` must also be
     cleared, since the GTK theme plugin opens a display of its own and
     aborts without one. `run-qt-tests.sh` sets both.
   - **What it checks:** every dialog opens and reports the expected
     window title; every context menu entry resolves to a real menu bar
     item; every hotkey resolves, is a sequence Qt understands, is
     actually attached, and is unique; every chart type renders a
     non-blank image of the right size.
   - **Deliberately says nothing about menu content** — not labels, not
     order, not counts. Edit the context menus freely without touching
     tests; they fail only when an entry points at something that isn't
     there. Dialog window titles *are* asserted.
   - **It found two real bugs on its first run**, both shipped earlier
     the same day: `Shift+V` was bound to a label scraped out of a
     *comment* rather than the menu item, and Nearest Cities crashed the
     process because `FBmpDrawMap2()` had the same missing QT guard
     `FBmpDrawMap()` was fixed for — on Qt `bmp` stayed pointing at the
     never-allocated export buffer.
   - Because rendering goes to `gi.qim`, a QImage in memory, pixel level
     regression tests are possible here with no screenshotting at all.
     The current check is only "did anything draw"; comparing against
     stored baseline hashes is the obvious next step, and would cover the
     kind of rendering question that has previously been argued over from
     screenshots.

4. **Pixel-level baselines.** Rendering goes to `gi.qim`, a QImage in
   memory, so image regression tests need no screenshotting at all.
   Storing baseline hashes per chart type is the obvious next step and
   would settle the kind of rendering question this project has
   repeatedly argued over from screenshots. The Wine build gives a
   reference to generate them against.
5. **Decide about the deliberate divergences.** The behaviours in
   "Known divergences from Windows" are places this port knowingly does
   something different, usually because Windows' behaviour looks like a
   bug. They are defensible individually, but if the goal is strict
   parity they are the list to revisit. The menu accelerator column
   (`Shift+V` where Windows writes `V`) is the most visible one and the
   only one a user sees on every menu.
6. **Unfinished business, low value:** Wingdings and the plain text
   fonts aren't bundled (see item 15); the black wedges in the tick ring
   are unexplained upstream rendering (see item 11) and nobody has
   actually worked out what draws them.
7. **Use it on real data.** Added 2026-08-26, and on the evidence it
   outranks everything above it. Every verification before that date was
   mechanical — does the item fire, does the dialog open, does the layout
   match. One session driving the port from a user's own settings file
   found three bugs (items 27-29) that the 2728 assertions of the day
   and three resource audits had all missed, because each was in behaviour
   nothing measures: process startup, keyboard focus, and exit cleanup.
   Run charts you actually want to read, from a settings file that isn't
   the repo's.
8. **~~The switch families Save Program Settings drops.~~** — all fixed
   2026-08-26; see "Features this fork adds to both builds", which has the
   list and two worked examples. Found the way item 7 recommends, by
   running a real config through the GUI and diffing what came back.
   **What is still open is one line:** `case 'W':` in astrolog.cpp is
   `#if defined(WIN) || defined(QT)`, so the console build rejects `-WM`
   as an unknown switch and stops reading the rest of the file. Saved
   settings files are therefore not portable to it.
9. **~~A way to render a chart without the event loop.~~** — **done
   2026-08-26.** It already half existed and the claim that it didn't cost
   real time; see item 34. `TextChartCaptureQt()` in qttest.cpp had always
   rendered text charts to PNG with no display, and now
   `GraphicsChartCaptureQt()` beside it does the other 24 modes:

       QTGRAPHDIR=out/qtg ./run-qt-tests.sh

   Both capture and exit without running the assertion suite. The whole
   graphics set takes about three seconds — every mode draws in 1-60ms
   except Rising at ~490ms and the two transit grids at ~275ms.
   This is what item 4's pixel baselines want.

**If upstream releases a new Astrolog**, this fork's changes to shared
code come in two kinds and they merge differently.

*Porting* changes are small and confined to `#ifdef QT` branches;
`grep -ln "ifdef QT" *.cpp *.h` finds all of them.

*Features added to both builds* are not guarded at all, on purpose —
`calc.cpp` has zero `ifdef QT` in it, and the Object Selections dialog
lives in `astrolog.rc` and `wdialog.cpp` alongside upstream's own. They
are shaped to be offerable upstream rather than to be easy to strip, so
expect real merge work there. See "Features this fork adds to both
builds".

Diff against the upstream tarball rather than assuming, and keep line
endings intact when editing those files, which is its own trap (see
"Working pattern" at the end).

## How this fork's Qt backend works

- **Multi-backend selection**: `astrolog.h` uses `#ifdef X11` / `WIN` /
  `WCLI` mutual exclusivity (`#error` if more than one defined). `QT` was
  added following the same pattern — pass `-DQT` on the compile command
  line; `#if !defined(QT) #define X11 ... #endif` in astrolog.h means QT
  overrides the default without editing the file. Qt's own headers
  (`QApplication`, `QMainWindow`, etc.) must be `#include`d at the very
  top of astrolog.h, before any Astrolog macro is defined — Astrolog
  defines bare words like `META`/`PS`/`TIME` that collide with identifiers
  Qt's headers use internally.
- **Shared device layer**: `xgeneral.cpp`/`xscreen.cpp`/`xcharts0-2.cpp`/
  `xdevice.cpp`/`xdata.cpp` are NOT X11-exclusive despite the "x" prefix —
  they contain interleaved `#ifdef X11`/`WINANY`/`QT` branches inside
  individual functions, shared across backends. Only `wdialog.cpp`/
  `wdriver.cpp` are truly Windows-only, and only `qtdialog.cpp`/
  `qtdriver.cpp` are Qt-only.
- **`FActionX()`** (xscreen.cpp) is the single shared entry point for
  casting + drawing a graphics chart, used by every backend. It derives
  `gi.fFile` from `gs.ft` (`gi.fFile = (gs.ft != ftNone)`) and takes a
  file-writing path instead of drawing to screen when so — the exact same
  mechanism `-Xb`/`-Xp`/etc command line switches already use. See "Two
  separate rendering paths" below — this is the *graphics* path only.
- **`Action()`** (astrolog.cpp) is the actual single top-level dispatcher
  X11/console command-line mode calls once per chart: casts the chart,
  then `if (us.fGraphics) FActionX(); else PrintChart();`. This is the
  *text* path — see below.
- **Menu-command pattern**: Windows' `NWmCommand()` (giant `switch(wCmd)`)
  sets pending state (`wi.nMode`, flags) → `ProcessState()` applies it
  (recast/redraw/menu-sync) on the next message loop pass. Qt doesn't need
  this indirection (no message queue to defer into) — each `QAction`'s
  lambda applies its effect immediately and redraws itself.
- **Generic helpers** in qtdriver.cpp, used everywhere instead of hand
  rolling each menu item: `AddToggleAction(menu, label, flag *pfield,
  flag fRecast)`, `AddSelectAction(menu, QActionGroup*, label, value, int
  *ptarget, flag fRecast)`, `AddChartModeAction(menu, label, mode)` (chart
  type/mode radios — see below), `AddCategoryRestrictAction(menu, label,
  flag *pfield, lo, hi, except)` ("Include <category>" toggles),
  `AddRelAction`, `AddAnimRateAction`/`AddAnimFactorAction`. Reach for
  these first; only write a bespoke handler when the field isn't a plain
  bool/int (see "inv() on multi-value fields" below) or has a side effect
  beyond "set field, redraw."
  Note `AddCategoryRestrictAction()` takes a trailing `flag fTransit`
  telling `SyncRestrictMenuQt()` whether that category counts the transit
  set (see gotcha #9).
- **Dialog helpers** in qtdialog.cpp, all near the top of the file so they
  precede every caller. Use these rather than reinventing a field:
  - `SzFormatRQt(real, int n)` — format a real the way Windows'
    `SetEditR()` does, via Astrolog's own `FormatR()`. **Never print a
    real with `QString::number()`**; it shows full double precision where
    Windows shows two or three digits. Sign convention is easy to get
    backwards: negative `n` strips trailing zeros ("7", "0"), positive
    `n` keeps at least one fractional digit ("1.0").
  - `NewComboQt(strCur, rgstr)` — an editable combo with dropdown
    suggestions, the counterpart of Windows' `SetEdit()` + `SetCombo()`
    pairs. Its companions `RgstrMonthQt()`/`RgstrDayQt()`/`RgstrYearQt()`/
    `RgstrTimeQt()`/`RgstrDstQt()`/`RgstrZoneQt()`/`RgstrLonQt()`/
    `RgstrLatQt()` hold the chart info suggestion lists.
  - `NewColorComboQt(ki, nExtra)` / `NColorFromComboQt(pcb)` — color
    pickers, sized by `SetEditColor()`'s `nExtra` convention.
  - `BlockComboWheelQt(&dlg)` — call before every `exec()`, see gotcha #10.
  - `ParseCustomDefQt()` — the Object Customization definition-string
    parse, in one place because it's wanted in three.
  - `RESBUT` + `ShowRestrictRangeDialogQt()` — one implementation behind
    all four restriction dialogs, with their quick-button columns
    described as data.
  **QComboBox ordering trap:** every `addItem()` must precede
  `setEditText()`. Adding the first item resets the current index, which
  silently overwrites the edit text — this has caused a real bug twice.
- **Chart type/mode**: `SetChartModeQt(int mode)` (qtdriver.h) is a direct
  port of `ProcessState()`'s chart-type switch — clears every known
  chart-type `us.f*` flag, sets `gi.nMode = mode` **directly** (not via
  `DetectGraphicsChartMode()` re-derivation like Windows does — that
  function has real gaps, see gotcha #4 below), sets the one matching
  `us.f*` flag if any, syncs the shared `QActionGroup`'s checked state, and
  redraws. The Chart menu's 16 items *and* the Graphics menu's 5 view-mode
  items *and* the Setting menu's Planetary Moons chart types all share one
  `QActionGroup` (`s_pgroupChartMode`, file-scope in qtdriver.cpp) because
  Windows treats chart type as one unified radio state regardless of which
  menu changed it. `RecastAndRedrawQt()`/`RedrawQt()` are the two redraw
  entry points (recast = positions may have changed; redraw = only
  appearance changed).
- **Two separate rendering paths** (the single most important gotcha in
  this codebase for anything touching chart display): graphics
  (`FActionX()`/`DrawChartX()`, driven by `gi.qpaint`/`gi.qim`) and text
  (`Action()`/`PrintChart()`, driven by `is.S`/`is.szFileScreen`) are
  **completely separate**, chosen by `us.fGraphics`. `RedrawQt()` branches
  on this: graphics mode does the normal `gi.qim` blit; text mode
  (`us.fGraphics == fFalse`) calls `RedrawTextQt()` (added for Colored
  Text/Show Interpretations — see the commit history), which points
  `is.szFileScreen` at a temp file, forces `us.fTextHTML = fTrue` (so
  color comes from real `<font color>` tags instead of needing an ANSI
  escape parser), calls `Action()`, reads the temp file back, and shows it
  in a persistent `QTextBrowser` window (`s_pdlgText`/`s_ptextBrowser`,
  file-scope in qtdriver.cpp), hidden again once graphics mode returns.
  **Any new feature that sets `us.fGraphics = fFalse` and calls
  `RedrawQt()`/`RecastAndRedrawQt()` gets this text window for free** —
  don't build a separate text display mechanism.
- **Command line escape hatch**: `ShowCommandLineDialogQt()` (Edit menu)
  routes through Astrolog's own `NParseCommandLine()` + `FProcessSwitches()`
  — exposes every switch Astrolog understands, not just what has a
  dedicated menu item. Point users here for anything missing.

## Gotchas worth knowing before you touch related code

1. **`wi`/`WI` struct is Win32-only.** Entirely gated behind `#ifdef
   WIN`/`WCLI` in astrolog.h (two different `WI` struct definitions, one
   per backend, both excluding QT). Grep for `wi.` before assuming a field
   Windows uses is portable — `us`/`gs`/`gi` almost always are, `wi` never
   is. Hit this porting "Show Constellation Lines" (needed a Qt-local
   static flag, `s_fStarLine`, instead of `wi.fStarLine`) and would hit it
   again for anything touching `wi.hdc`/`wi.nTimerDelay`/etc (Graphics
   Settings and File Settings dialogs both have a few such fields — see
   below).
2. **`ResizeWindowToChart()` is `#ifdef WINANY`-only** (GetDC/
   GetDeviceCaps/window-rect Win32 calls) — ports don't apply. Where
   Windows resizes the OS window to match a computed chart size, this
   fork resizes the Qt window itself instead (`gi.qwind->resize(...)`,
   same call the startup code already uses) since the Qt canvas takes
   whatever size the window gives it, not the reverse. Used for Square
   Screen; the same pattern applies to `DlgFile`'s window-size fields if
   ever ported.
3. **`FActionX()` is normally called exactly once per process** (right
   before it exits, for a command-line export) but this fork calls it
   repeatedly within one long GUI session. It only restores
   `gs.xWin`/`yWin`/`nScale` for *some* formats internally (temporarily
   multiplies them by `PSMUL`/`METAMUL`/`SVGMUL`/`WIREMUL` per format and
   divides back — except for `ftBmp`, which just clamps to
   `BITMAPX`/`BITMAPY` and never un-clamps). `FExportChartQt()`
   (qtdialog.cpp) saves and unconditionally restores all three plus
   `us.fGraphics` around every call, rather than trusting `FActionX()`'s
   partial internal restore. Follow this pattern for any new caller of
   `FActionX()`.
4. **`DetectGraphicsChartMode()` (xscreen.cpp) has real gaps** — doesn't
   cover several chart-type flags (`fListing`, `fAspList`, `fArabic` among
   them), and `DrawChartX()` has no fallback if `gi.nMode == 0`. Windows'
   `ProcessState()` zeroes `gi.nMode` and trusts this function to
   re-derive it before the next draw; that produces a blank chart for
   those modes here. `SetChartModeQt()` avoids the whole problem by
   setting `gi.nMode` directly instead of zeroing and re-deriving. If you
   ever add a new chart mode, make sure it goes through `SetChartModeQt()`
   the same way rather than mimicking Windows' zero-and-redetect.
5. **Adding a chart mode is one edit.** *(Revised 2026-08-25 — this
   used to warn that it took two, a clear-list entry and a switch case,
   which could silently drift apart. They are now a single
   `rgchartmodeQt[]` table of mode/flag pairs in qtdriver.cpp, read by
   `SetChartModeQt()` and by the command-line/macro sync described in
   item 13.)* Add the pair to that table; if the mode also wants a menu
   entry, add it with `AddChartModeAction()` as usual.
6. **`inv()` on a non-boolean field is often intentional, not a bug** —
   e.g. `inv(us.nDwad)` (dwad nesting *level*, an int) and `inv(us.nAppSep)`
   (3-way aspect-orb-type enum) both collapse to a 0/1 toggle in Windows
   itself. Verified by reading the actual Windows handler before "fixing"
   anything that looks like this — matching Windows' quirk exactly is
   correct, not a shortcut.
7. **The shared `s_pgroupChartMode` `QActionGroup` must be allocated
   before *any* menu that adds to it is built**, not lazily inside
   whichever menu-builder happens to add the first item. It's allocated
   once in `BuildAstrologMenus()` before any `BuildXxxMenu()` call. Real
   bug hit once already: when `BuildSettingMenu()` (Planetary Moons chart
   types) started adding to this group and runs before `BuildChartMenu()`
   in menu-bar order, code that assumed `s_rgpaChartMode[0]` was always
   "Standard Radix" started checking the wrong item. Fixed by looking up
   the Wheel entry by mode value, and by hoisting the allocation — but the
   general lesson is: don't assume anything about *index* into the shared
   tracking arrays (`s_rgpaChartMode`/`s_rgnChartMode`/`s_cChartMode`),
   only ever look up by mode value.
8. **Compile-time feature macros are effectively always on.** `SWISS`,
   `PLACALC`, `MATRIX`, `JPLWEB`, `CONSTEL`, `ARABIC`, `BIORHYTHM`, `PS`,
   `META`, `SVG`, `WIRE` are all `#define`d by default in astrolog.h and
   nothing in `Makefile.qt` undefines any of them — so don't assume a
   Windows feature gated behind one of these is unavailable without
   actually checking; it's compiled into the Qt build too.
9. **Menu-checkmark staleness: follow Windows, case by case.** *(Revised
   2026-08-25 — this used to say staleness was an accepted trade-off not
   worth chasing. That was wrong, and the 8.9/8.12 sweeps found four real
   cases.)* The rule is simply what Windows does: wherever a `Dlg*`
   function calls `WiCheckMenu()` before returning, the Qt dialog owes the
   same resync, and where it doesn't, neither do we. Four sync functions
   exist, all declared in qtdriver.h and called at the end of the matching
   dialog:
   - `SyncHelioMenuQt()` — Heliocentric, from Calculation Settings.
   - `SyncRestrictMenuQt()` — the six "Include <category>" entries *and*
     the `us.f*` flags behind them, from all four restriction dialogs.
     Note it derives each category from `ignore || ignore2` except fixed
     stars, which `DlgStar` derives from `ignore` alone.
   - `SyncHouseSetMenuQt()` — House Settings' Solar Chart, 3D Houses,
     Show Dwads, from Calculation Settings.
   - `SyncDisplayMenuQt()` — View's Print Nearest Second and Applying
     Aspects, from Display Settings.
   Genuinely-matching-upstream staleness still exists and is fine: the
   House System submenu doesn't resync when Calculation Settings changes
   `us.nHouseSystem`, because `DlgCalc` doesn't `WiCheckMenu` it either.
   Still don't build a general `RedoMenu()`-style blanket resync — check
   the specific `Dlg*` and mirror it.
10. **Wheel events edit combo boxes in the scrolling dialogs.** Qt
   delivers a wheel event to whatever widget is under the pointer, so
   scrolling a tall dialog silently changed any combo that slid past the
   cursor. `BlockComboWheelQt()` (qtdialog.cpp) fixes this and is called
   before every `exec()` — **keep calling it in any new dialog**. Beyond
   the fix, the debugging lesson: a value read off a screenshot taken
   after scrolling is not trustworthy. Confirm against `astrolog.as` or
   the `GS`/`US` initializers before concluding a dialog is buggy.

## Status by menu

Legend matches `QT_MENU_MAPPING.md`: `[D]` dialog, `[T]` toggle, `[S]`
select-one, `[A]` one-shot action, `[P]` submenu.

### File — COMPLETE except the wallpaper modes
Done: Open Chart..., Save Chart Info..., Save Chart Positions..., Save Program
Settings..., Other Formats submenu (Save Chart Exchange/AAF, Save Chart
Quick*Chart, Save Chart iCalendar — done 2026-08-24, see
`ShowSaveAAFDialogQt()` etc. in qtdialog.cpp), Export Chart Text
Output..., Export Chart Bitmap..., Export Vector Format submenu (Metafile/
PostScript/SVG/Wireframe), Open Bitmap submenu (Open Chart Background,
Open World Map — also done 2026-08-24), File Settings... (done
2026-08-24, `ShowFileSettingsDialogQt()` — the portable subset of
`DlgFile`; skipped `wi.fBmpWindow`/`wi.nAntialias`/`wi.fNoPopup` as
Win32-only — "Use Real System Fonts" was skipped alongside them and is
now in, see item 15), Exit.
Open Chart #2 and Other Formats' Open Charts in Folder.../Save Chart
List... are done too — see Info below, since they belong to the chart
list. Print... is done; see item 11.

Not done, and both deliberate:
- Export as Wallpaper `[P]` (5 variants) — sets desktop wallpaper, a
  concept that doesn't map cleanly to modern Linux desktop environments
  (no single portable "set wallpaper" API the way Win32 has one). Skipped;
  five of the twelve items the parity test skips on purpose. If it's ever
  wanted, export the bitmap and leave setting the wallpaper to the desktop.
- Print Setup... `[D]` — a native Windows print dialog. Not applicable;
  `QPrintDialog` already carries the setup UI Windows splits out.

### Edit — COMPLETE
Done: Enter Command Line... (the escape hatch — see above), Paste (item
9), all 96 macro slots (item 10), Copy Chart
Text Output, Copy Chart Bitmap, Copy Vector Format submenu (Metafile/
PostScript/SVG/Wireframe) — done 2026-08-24, see `CopyChartTextQt()`/
`CopyChartBitmapQt()`/`CopyChartVectorQt()`. Copy Bitmap is trivial
(`gi.qim` is already a `QImage`). Copy Vector Format reuses
`FExportChartQt()` to a scratch temp file instead of one the user keeps;
text formats (PS/SVG/Wireframe) go on the clipboard as plain text (SVG
also gets `image/svg+xml` set alongside), Metafile (binary) gets
`image/x-wmf` only.

**Real crash found and fixed here, worth knowing about**: `Action()`
branches on `us.fGraphics` ("if (us.fGraphics) FActionX(); else
PrintChart();"). `CaptureTextChartQt()` (the shared text-capture helper
`RedrawTextQt()` already used) didn't force `us.fGraphics` false itself —
it relied on every *caller* doing that first, which was true for
`RedrawTextQt()`'s existing callers (they all go through `RedrawQt()`'s
own text-mode branch, which already guarantees it) but not for
`CopyChartTextQt()`, a new caller that invoked it directly. With
`us.fGraphics` still true, `Action()` took the graphics path, which for
QT calls `InteractQt()` again — a second, nested Qt event loop
("QCoreApplication::exec: The event loop is already running"), silently
producing empty output. **Lesson: push invariants like this into the
shared helper itself, not onto every caller to remember** — fixed by
having `CaptureTextChartQt()` save/force/restore `us.fGraphics` around
its own `Action()` call, so no future caller can hit this. Root-caused
with the gdb debug-build workflow (see below) after black-box testing
didn't converge — `pgrep astrolog-qt-debug` (no `-f`) gave false
"not running" reads because Linux's `/proc/pid/comm` truncates at 15
characters; use `pgrep -f` (matches full cmdline) when a binary's name is
long, or `pgrep` will lie to you.

### View — COMPLETE except Buffer Redraws
Done: Show Graphics, Colored Text, Redraw Screen, Set Colors..., Show
Interpretations, Print Nearest Second, Parallel Aspects, Applying
Aspects, and the Window Settings `[P]` submenu (item 18 — this section
used to say the whole submenu was skipped as Win32-only, which is no
longer true).

**Buffer Redraws** is the one item still absent, deliberately: it toggles
whether Win32 draws through an off-screen bitmap, and Qt composites every
widget off-screen regardless, so there is nothing for it to switch. An
item that silently does nothing would be worse than not offering it.

### Info — COMPLETE
Done: Set Chart Info..., Chart for Now, Default Chart Info..., all 8
relationship chart type radios, Set Chart #2 Info..., Charts #3 Through
#6... (`ShowChartsAllDialogQt()`), and the Chart List submenu (Chart
List... dialog, Previous/Next/First/Last Chart, Swap Chart #1 and #2).

The six **chart slots**: `rgpci`/`rgpcp` (extern.h, `cRing = 6`) are the
per-ring chart info/positions, slot 1 the main chart and 2-6 the extra
rings a bi/tri/.../hexa wheel draws. `ShowChartInfoForQt(CI *, title)`
edits any slot; `FOpenChartIntoQt(iChart, file)` loads a file into one.

The **chart list** (`is.rgci` / `is.cci` / `is.iciCur`):
`ShowChartListDialogQt()` ports `DlgList`. Note `is.iciCur` starts at
**-1**, not 0, which matters for the navigation clamping.
`FSortCIList`/`FAppendCIList`/`FilterCIList`/`FEqSzSubI` all live in
general.cpp and are fully portable. The list is populated by
`FInputData()` itself, which calls `FAppendCIList()` when reading a multi
chart file (AAF, Quick*Chart, Astrodatabank, Solar Fire text) — an
earlier claim here that nothing populated it was wrong. Open Charts in
Folder is written on QDir rather than ported, because the shared
`OpenDir()` in io.cpp has its entire body inside `#ifdef PC`.

All of this is verified live, including Open Charts in Folder (a folder
of three .as files loads with correct weekdays and degree signs) and Save
Chart List (writes a correct `@AL` header and one `-qcl` line per chart).
Code review of those two beforehand also turned up and fixed a real NULL
deref crash — see the commit "Fix NULL deref saving a chart list with an
unnamed chart".

**Note for testing anything in this port:** the Qt build has no headless
mode — it always enters the Qt event loop, so command line style checks
(`./astrolog-qt -i foo.as -o bar.as`) just hang rather than running and
exiting. Verification has to be either through the GUI or by reasoning
about shared code in io.cpp/general.cpp, which the console build does
exercise.

### Setting — COMPLETE, plus one item Windows didn't have
All items done: Sidereal Zodiac, Heliocentric, House System (22-item
submenu), House Settings submenu (Solar Chart, 3D Houses, Decans, Dwads,
Flip Signs, Geodetic, Indian Wheel Order, Navamsas), Aspect Settings...,
Object Settings..., More Object Settings..., Restrictions...,
Star Restrictions..., Transit Restrictions..., Planetary Moons submenu
(Moons/Exoplanets chart types, Moon Restrictions..., Moon Object
Settings..., Object Customization..., Star Customization...), 7 Include-
category toggles (Minors/Cusps/Uranians/Dwarfs/Fixed Stars/Moons/COB —
note Include Moons/COB are flattened to top-level here rather than nested
under Planetary Moons like Windows; a deliberate simplification, not a
gap), Calculation Settings..., Display Settings....

Also **Object Selections...** (Ctrl+T), which this fork adds to *both*
builds rather than only this one — see "Features this fork adds to both
builds" near the end. It sits after More Object Settings, matching the
resource, so menu parity counts it on both sides rather than needing an
exemption. Its accelerator is in the resource's own table for the same
reason. No plain Alt+letter was available: the menu mnemonics take
F E V I S C G A H and the accelerator table takes the rest, and the
whole Alt+Shift range is in use.

### Chart — COMPLETE
All 16 chart-type radios, Transits..., Progressions..., Chart Settings....

### Graphics — COMPLETE
Done: 5 sphere/globe/map view types, Reverse Background, Monochrome,
Square Screen, Character Scale submenu, Chart Effects submenu, Map
Effects submenu (including Show Constellations/Constellation Lines/
Detailed World Map), Map Orientation submenu (rotate/tilt/zoom), Indian
Style Charts submenu, Modify Display, Modify Chart, Scribble Color
submenu (16 colors), Graphics Settings... (done 2026-08-24,
`ShowGraphicsSettingsDialogQt()`).

Notes on the Graphics Settings port:
- Several fields intentionally overlap menu items already built
  (Character Scale, Map Orientation, Modify Chart). That matches Windows:
  the menus step values coarsely, the dialog is for typing an exact
  value. No attempt is made to resync those menus' checkmarks afterward
  if a typed value doesn't match a preset (see gotcha #9).
- Skipped as Win32-only (`WI` struct): "Don't Automatically Redraw
  Screen" (`wi.fNoUpdate`). The animation update delay was skipped for
  the same reason and is now **in** — item 12 gave the Qt build a real
  animation loop, so `s_nTimerDelay` stands in for `wi.nTimerDelay`.
- The six font selection combos (`gs.nFontTxt`/`Sig`/`Hou`/`Obj`/`Asp`/
  `Nak`) were once skipped as unportable Windows GDI face names. That was
  only half true — the fonts ship *with Astrolog*, in `font/`. Item 15
  bundles them and gives `DrawGlyph()`/`DrawSzFont()` their Qt branches.
- **Deliberate deviation from Windows**: `DlgGraphics`' "Atlas City
  Coloring" combo writes `gs.fLabelAsp`, but that field is `-XA` (draw
  aspect glyphs on lines — the Chart Effects toggle) and is unrelated to
  cities; the `-XL` switch that owns `gs.nLabelCity` toggles
  `gs.fLabelCity` (xscreen.cpp). Reproducing it faithfully would make the
  combo silently toggle aspect glyphs, so this port writes
  `gs.fLabelCity`. Upstream typo, not a porting choice — worth reporting
  upstream if anyone ever files bugs against CruiserOne/Astrolog.
- The wheel-corner/fill/city label tables live in `wdialog.cpp` (not
  compiled into the QT build), so qtdialog.cpp has its own copies,
  including Windows' non-array display order for corner types. Keep in
  sync if upstream adds a mode.

### Animate — COMPLETE
Do Animation, Jump Rate submenu (Update to Now + 9 rate values + 3
fractional-second values), Jump Factor submenu (9 unit values), Reverse
Direction, Pause Animation, Timed Exposure, Step Forward/Backward, Store/
Recall Chart Info.

### Help — COMPLETE except Setup (not applicable)
Done: Open Documentation..., Open Changes, Open License, Open Default
Settings, Open Orbital Elements, Open Star List, Open Atlas, Open Time
Zone Changes, Open Exoplanet List, Open Website, Open Website Mirror,
About Astrolog..., and **all 11 List Signs/Objects/Aspects/
Constellations/Planet Info/Rays/General Meanings/Switches/Obscure
Switches/Keystrokes/Credits actions** (done 2026-08-24 — see
`AddChartModeTextAction()` in qtdriver.cpp, which also now backs the
Exoplanets Chart action; joins `s_pgroupChartMode` exactly like every
other chart mode, confirmed correct via `astrolog.cpp`'s own
`Assert(rgcmdMode[gSign] == cmdHelpSign)`-style self-checks). Fixed along
the way: `AddChartModeTextAction()` keeps the View menu's "Show Graphics"
checkbox in sync (`s_paGraphics`, file-scope) the same way Colored Text/
Show Interpretations already did — a real gap since forcing text mode via
the new helper wasn't updating it before. **Naming trap worth
remembering**: `us.fConstel` (List Constellations) is a *different field*
from `gs.fConstel` (Graphics > Map Effects > Show Constellations, a
separate already-implemented toggle) — easy to transpose since they read
almost identically.

Open Website/Website Mirror (done 2026-08-24) needed a different
mechanism from the plain doc/data openers: `astrolog.url`/`astrlog2.url`
are Windows `.url` shortcut files (simple INI format) whose *content* is
the URL to open. Read via `QSettings(path, QSettings::IniFormat)` and the
key `"InternetShortcut/URL"` instead of opening the file itself, since
`.url` files happen to already be valid INI — no manual parsing needed.

Missing: Setup `[P]` submenu — Windows installer only, not applicable,
skip.

## Work log — items 1-34

Kept because each entry records what was actually found, which is more
useful than the fact that it's finished. Several were not what their
original description said they were.

Items 1-15 are completed pieces of work. Items 16-34 are findings — how
a thing turned out to work, or a class of bug worth not repeating — and
are the more useful half to read before starting something new.

1. ~~Help's 11 list actions~~ — **done 2026-08-24**, see Help section
   above.
2. ~~File's remaining standalone export/save variants~~ — **done
   2026-08-24**, see File section above.
3. ~~Help's remaining doc/data file openers~~ — **done 2026-08-24**, see
   Help section above. Help menu is now fully complete.
4. ~~Edit menu Copy Bitmap/Text/Vector Format~~ — **done 2026-08-24**, see
   Edit section above.
5. ~~File Settings dialog~~ — **done 2026-08-24**, see File section above.
6. ~~Graphics Settings dialog~~ — **done 2026-08-24**, see Graphics
   section above. The font pickers were left out here and added by item 15.
7. ~~Info's multi-chart feature~~ — **done 2026-08-25** (chart slots and
   chart list both); see Info section above, including the two paths that
   were verified live afterwards. With this the **menu structure is
   complete**: every top-level menu and dialog Windows has is present
   apart from the deliberate Win32-only omissions.
8. ~~**UI parity sweep, one dialog at a time.**~~ — **all sub-items done
   2026-08-25.** Every dialog below was
   functionally correct but presentationally diverged from Windows.
   Requested after the chart info dialog was caught showing `21.9` where
   Windows shows `9:57pm`. Each sub-item is independently doable and
   committable — take them one per iteration, with a screenshot check
   against the matching `Dlg*` in wdialog.cpp and resource block in
   astrolog.rc. **The fix pattern for all the formatting ones is already
   written**: see `ShowChartInfoForQt()` — `szMonth[]` for months,
   `SzTim()`/`SzZone()`/`SzLocation()` (the last with `us.fAnsiChar`
   forced off, split at `is.ichLocSplit`). Display-only: Astrolog's
   `NParseSz`/`RParseSz` already accept these forms on the way back in.

   8.1 ~~Chart Info (`DlgInfo`)~~ — **done 2026-08-24**, and is the
       reference implementation for the rest.
   8.2 ~~Default Chart Info~~ — **done 2026-08-25**: zone/longitude/
       latitude now show as `8W` / `122:19W` / `47:36N`, daylight saving
       as No/Yes/Autodetect, labels and field order per the `.rc`.
       Verified live including an OK round trip.
   8.3 ~~Transits~~ — **done 2026-08-25**: formatting fixed, and the
       Times and Graph Cover range, six Transit Time Restrictions, four
       display toggles, Searching Divisions and the Now button all added.
       Verified live. Note the cover range is derived from
       `us.fInDayMonth`/`us.fInDayYear`/`us.nEphemYears` rather than
       stored as an index — that asymmetric logic is ported as-is.
   8.4 ~~Progressions~~ — **done 2026-08-25**: formatting fixed, the
       progression type radios relabelled to Windows' wording, rate and
       cusp ratio turned into editable combos carrying Windows' presets,
       the `X` reciprocal-rate prefix supported, and a Now button added.
       Verified live. **Gotcha found here, applies to any editable
       QComboBox in this port**: all `addItem()` calls must precede
       `setEditText()`, since adding the first item sets current index 0
       and overwrites the line edit — otherwise any value that doesn't
       match a preset displays as the first preset.
   8.5 ~~Colors~~ — **done 2026-08-25**: full 16 slot palette (via
       `ikPalette[]`, whose guard was widened to QT), elements, seven
       rays, scribble pen and wheel corners, all as colour name combos.
       Verified live. **Fixed a bug that predated it**: `InitColors()`
       (astrolog.cpp) derives the `kObjA[]` table the drawing actually
       reads from `kElemA`/`kRayA`, and `RedrawQt()` never called it, so
       element and ray colours could be saved but never took effect. Now
       called at the top of `RedrawQt()`, matching `Action()`. Note
       `InitColorsX()` (xscreen.cpp) is a *different* function — backend
       palette setup — and is still needed separately.
   8.6 ~~Chart Settings~~ — **done 2026-08-25**: astrocartography step/
       crossings/distance, the two sort order radio groups, aspect sort
       order and the wheel subdivision type all added, and the checkbox
       wording now follows the `.rc`. Verified live. Note the sort orders
       are stored as switch letters not indexes, and the subdivision type
       splits across `us.fListDecan` + `us.nDecanType`.
   8.7 ~~Object / More Object / Moon Object Settings~~ — **done 2026-08-25**
       (`DlgObject`, `DlgObject2`, `DlgObjectM`): all three grids now
       format through the new `SzFormatRQt()` helper (a thin wrapper on
       `FormatR()`, which is what Windows' `SetEditR()` calls) using the
       same precisions Windows does — `-2` max orb, `-1` orb addition,
       `-2` influence. The main Objects dialog was also missing its
       Influence column (`rObjInf`) entirely; added. Color columns went
       from free-text `QLineEdit` to the `NewColorComboQt()` drop-down,
       sized with `SetEditColor()`'s `nExtra` convention: 1 for objects,
       `1 + (i == starLo)` in the More Object grid (Windows widens the
       list by one on the collective stars row), 3 for the moons.
   8.8 ~~Aspect Settings~~ — **done 2026-08-25** (`ShowAspectDialogQt` /
       `DlgAspect`): `-6` orb, `-6` angle, `2` influence, color drop-down
       with `nExtra` 0. Note `FormatR()`'s sign convention — negative n
       strips trailing zeros ("7", "0"), positive n keeps at least one
       fractional digit ("1.0"), which is why influence reads 2 and not
       -2. Dialog widened 500 → 620; the extra "Show" checkbox column was
       pushing Color off the right edge.
   8.9 ~~Restrictions / Star / Transit Restrictions~~ — **done 2026-08-25**
       (`DlgRestrict`, `DlgStar`). This turned out to be much more than
       the missing buttons the item originally described:
       - **Range was wrong.** Both dialogs showed objects `0..oCore`
         (oCore is 21). Windows shows `0..dwarfHi` — the ~30 cusps,
         Uranians, and dwarfs were entirely unreachable from the Qt GUI.
       - **Checkbox polarity was inverted.** Qt labelled them
         "Show <name>" with checked = visible; Windows uses the bare
         object name with checked = *restricted*. Flipped to match
         Windows. This one is worth remembering: it's the difference that
         silently produces the opposite chart for someone acting on
         muscle memory.
       - **Titles** now match: "Object Restrictions", "Transit Object
         Restrictions", "Fixed Star Restrictions".
       - **Quick-button column** added, driven by a `RESBUT` table
         (resSet/resClear/resToggle/resCopy) so all four restriction
         dialogs share one implementation, the way Windows shares one
         `DlgRestrict` between two of them.
       - **Menu checkmarks now re-sync.** Windows re-derives
         `us.fCusp`/`fUranian`/`fDwarf`/`fStar`/`fMoons`/`fCOB` and their
         menu checkmarks when these dialogs OK; Qt didn't. Added
         `SyncRestrictMenuQt()` in qtdriver.cpp, registered from
         `AddCategoryRestrictAction()`. Faithfulness detail: every
         category tests `ignore || ignore2` *except* fixed stars, which
         Windows' `DlgStar` derives from `ignore` alone — hence the
         `fTransit` field on `CATRES`.
   8.10 ~~Moon Restrictions~~ — **done 2026-08-25**
       (`ShowMoonRestrictDialogQt` / `DlgMoons`): all 9 buttons, using
       Windows' own labels including its "Tog. &Neptune" abbreviation.
       Windows writes the group toggles as offsets from the dialog's
       first checkbox (0-1 Mars, 2-5 Jupiter, 6-13 Saturn, 14-18 Uranus,
       19-21 Neptune, 22-26 Pluto, 27-31 COB); they're stored as absolute
       object indexes here, offset from `moonsLo`.
   8.11 ~~Object & Star Customization~~ — **done 2026-08-25** (`DlgCustom`,
       `DlgCustomS`). "Lookup Names" added to both: it fills in every
       display name still blank or `???` by resolving that row's
       definition, and leaves already-named rows alone. Objects go
       through `SwissGetObjName`/`szObjName`/`ObjMoons`/`ai[]` by
       definition type; stars go through `SwissTestStar()`, which
       rewrites its argument in place with the catalog's own spelling and
       returns false for anything the catalog doesn't have (→ `???`).
       Title also corrected to "Fixed Star Customization".
       - **Note:** definition type 4 (`j<n>`, JPL Horizons) fetches over
         the network synchronously while the modal dialog sits there,
         with no progress feedback. That's what Windows does too, so it
         was left as-is, but it's the obvious thing to make async if it
         ever proves annoying.
       - **Bug found and fixed while here.** The point/flag suffix parse
         (the `n`/`s`/`p`/`a`/`HSBNTV` letters after a space) was missing
         Windows' guard for an all-alphabetic definition, so a type-2
         object name read its own letters as flags: `Mar` → apsis,
         `Ven`/`Sun`/`Moon` → node, `Nep` → perihelion, `Vest` → south
         node. Only names with no such letter after position 0 (`Plu`)
         escaped. The parse is now in one place, `ParseCustomDefQt()`,
         instead of the two inline copies it had (Windows open-codes it
         twice as well).
   8.12 ~~Calculation Settings and Display Settings~~ — **done 2026-08-25**
       (`DlgCalc`, `DlgDisplay`). No fields were missing; the gaps were in
       formatting, parsing, wording, and menu sync.
       *Calculation Settings:*
       - **"D<n>" divisional-chart syntax didn't work.** Windows reads
         this field as `ChCap(sz[0])=='D' ? rDegMax/RFromSz(sz+1) : ...`,
         so "D9" means the navamsa (360/9 = 40). Qt used `toDouble()`,
         which returns 0 for "D9", so the dialog rejected it as an
         invalid harmonic factor. Now supported.
       - Zodiac Offset was a bare `QLineEdit`; Windows offers the ten
         named ayanamsas as "<offset> <name>" dropdown entries and reads
         the field with `atof()`, which stops at the space. Now an
         editable combo doing the same. House System combo made editable
         too (Windows' is `CBS_DROPDOWN`).
       - Reals now read through `RFromSz()`, what Windows uses, instead
         of `toDouble()` — same parse, and it also takes a leading `~`
         AstroExpression where that's compiled in.
       - Precision: `-6` harmonic, `6` zodiac offset (was full double).
       - Wording: "3D House Projection" → "3D Houses Plane", "Object on
         Angle" → "Solar Chart Setting", "Object:" → "Use This Planet:".
         "Use Start of Planet's Sign" moved inside the Solar Chart group
         where Windows has it, and the checkbox order now follows
         Windows' reading order.
       - **Menu sync missing**: `DlgCalc` re-checks House Settings' Solar
         Chart, 3D Houses, and Show Dwads. Added `SyncHouseSetMenuQt()`.
       *Display Settings:*
       - **"Applying Aspects" checkmark condition was wrong.** It was
         built with `AddToggleAction`, which checks `!= 0`, but `nAppSep`
         has three values and Windows checks `== 1` everywhere — so
         Waxing/Waning (2) showed as checked. Now has its own action.
         (The toggle itself stays `inv()`, which is what Windows does.)
       - **Menu sync missing**: `DlgDisplay` re-checks the View menu's
         "Print Nearest Second" and "Applying Aspects". Added
         `SyncDisplayMenuQt()`.
       - Display Format radios were in value order; Windows shows them
         27 Nakshatras (3) *above* 360 Degrees (2). Reordered for display
         while keeping the button ids equal to the values.
       - Precision `-6` on stationary velocity; "Antivertex" → "Antivert".
       - Both dialogs widened 450 → 530; the longest labels were clipped.
       - **Deliberate divergence, left as-is:** Windows' `nAsp` handler
         assigns `us.nAsp = na` *before* the loop `for (i = us.nAsp + 1;
         i <= na; i++) ignorea[i] = fFalse;`, so that loop can never run
         and raising the aspect count doesn't actually un-restrict the
         newly included aspects. Qt saves the old value first, so it
         works. Keeping the working version rather than reproducing the
         bug — flagged here so it reads as a choice, not an oversight.
   8.13 ~~File Settings and Graphics Settings~~ — **done 2026-08-25**
       (`DlgFile`, `DlgGraphics`). Wording and field lists were already
       right; the gaps were dropdowns, precision, and one grouping.
       - **Character Scale / Text Scale** were plain edit boxes. Windows
         offers both as editable dropdowns stepping 100..MAXSCALE by 50,
         except character scale, which lists only whole multiples of 100
         — matching `FValidScale` vs `FValidScaleText`. Both now do.
       - **Background Transparency Percent** likewise: Windows offers
         25/50/75/100 in an editable combo (`dcFi_XI1`).
       - Precision: `-3` background transparency, `-6` telescope/orbit
         zoom, `-3` map rotation and globe tilt. Reals read via
         `RFromSz()` as Windows does.
       - File Settings field order: Windows puts "Don't Show Background
         Bitmap" at the head of its right-hand column, directly above the
         transparency it governs, not in the left column's checkbox run.
       - "Animate Map Instead of Time" now sits in an **Animation** group
         box as Windows has it (its other two members are Win32-only).
       - The deliberate omissions stand and are still correct: the
         Win32-only `wi.*` fields, and the six font combos (they pick
         from a hardcoded list of Windows GDI face names; porting them
         means building a real Qt font picker, not translating a list).
       - **Cross-cutting bug found and fixed here.** Qt delivers wheel
         events to the widget under the pointer, so scrolling any of the
         tall settings dialogs silently changed whatever combo slid past
         the cursor — Wheel Fill, Character Scale, a color picker. This
         cost real debugging time: a screenshot taken after scrolling
         showed Wheel Fill as "7 Rays House" when `astrolog.as` plainly
         says `:Xv 1`, and it read exactly like a dialog bug. Windows'
         dialogs are fixed size with nothing to scroll, so the hazard is
         specific to this port. `BlockComboWheelQt()` (qtdialog.cpp) now
         swallows the wheel on any unfocused combo and forwards it to the
         enclosing scroll area, so the dialog scrolls and values hold
         still; it is called before every `exec()`. Click a combo first
         and the wheel adjusts it as normal.
       - **Note for whoever verifies these dialogs by automation:** don't
         wheel-scroll to reach a control and then trust what you read.
         Drag the scrollbar. The above is fixed now, but the general
         lesson stands — confirm a suspicious value against `astrolog.as`
         or the `GS` initializer in xdata.cpp before calling it a bug.

9. ~~Edit menu Paste~~ — **done 2026-08-25.** `PasteChartQt()` in
   qtdriver.cpp mirrors Windows' `FFilePaste()`: check the clipboard for
   an image first and text second, dump it to a temp file, and hand that
   to the same loaders the File menu already uses — `FLoadBmp()` into
   `gi.bmpBack` for an image, `FInputData()` for text. Warns with
   Windows' own wording when the clipboard holds neither.
   - One difference: Windows writes a raw `CF_DIB`, which has no
     `BITMAPFILEHEADER`, hence its `FLoadBmp(..., fTrue)`. `QImage::save()`
     writes a complete `.bmp`, so this passes `fFalse` and takes the same
     path File / Open Bitmap uses.
   - Verified both formats: a chart info file on the clipboard loads the
     chart (name, date, zone, location all correct), and an image loads
     as the background.
   - Observed while testing and since fixed: background bitmaps were
     never drawn at all in the Qt GUI. `FBmpDrawBack()` (xdevice.cpp) had
     a file-export path and a `#ifdef WINANY` path and nothing else, so
     interactively it drew nothing while still returning `fTrue` — which
     made callers believe a background was present and skip their own
     erase (the `dtErase` test at xcharts0.cpp:2590), blanking the chart
     backdrop instead. A `#ifdef QT` branch now blits the blended cache
     onto `gi.qim`. Note `FBmpDrawMap()`, immediately below it, already
     had a QT bail-out — the background case had just been missed.
10. ~~Edit menu's 96 macro slots~~ — **done 2026-08-25.** Eight submenus
    of twelve under Edit, split into two groups of four as Windows has
    them, each slot bound to F1-F12 under a different modifier
    combination via `QAction::setShortcut()` (Windows uses an accelerator
    table). `RunMacroQt()` calls `FProcessCommandLine(is.rgszMacro[i])`
    then recasts, which is what Windows' `cmdMacro01..96` handler does,
    including its two defaults for undefined slots: F1 opens
    astrolog.htm and Alt+F4 quits. Anything else undefined warns with
    Windows' exact wording.
    - Macros are *defined* the way they are on Windows — `-M0 <n>
      "<switches>"`, or in astrolog.as — not from the GUI. This menu only
      runs them, same as Windows'.
    - Windows can rename a macro's menu entry with `-WM`, which it does
      via `ModifyMenu` on Win32-only `wi.hmenu`. *(Corrected 2026-08-26 —
      this used to say "not ported". It is: `NProcessSwitchesQt()`'s `M`
      case clones the name into `rgszMacroQt[]`/`rgszMSubQt[]`, and
      `BuildMacroMenusQt()` reads them, so a settings file's `-WM` lines
      do rename the entries. See item 29 for the leak that came with it.)*
    - Verified: F5 with nothing defined gives "Macro number 5 is not
      defined."; a macro defined through the command line dialog runs and
      recasts when its key is pressed.

11. ~~File > Print...~~ — **done 2026-08-25.** `PrintChartQt()` in
    qtdriver.cpp, wired to File / Print. `QPrintDialog` + `QPrinter`, then
    the two things Windows' `DlgPrint()` does: scale the chart up before
    rendering, and force a white background when "Export Text and Print
    in Intuitive Manner" (`us.fSmartSave`) is on. Text charts go through
    `QTextDocument::print()` on the same HTML listing the text window
    shows, so Qt paginates them. Needed `Qt5PrintSupport` added to
    Makefile.qt. Verified by printing to PDF.
    - Windows draws straight onto the printer DC and scales by `METAMUL`
      (12), which is free for a vector DC. That isn't available here:
      `DrawFill()` (xgeneral.cpp) reads and writes `gi.qim` pixels
      directly, so `gi.qim` and `gi.qpaint` have to describe the same
      surface, meaning the chart must be rendered into a real QImage
      first. At METAMUL that would be a ~250MB image, so `PRINTMUL` is 4.
    - **A note on the black wedges in the tick ring.** These were once
      recorded here as a print-scaling artifact, caused by `DrawFill()`'s
      255-point queue (`iFillMax`) overflowing at PRINTMUL. That was
      wrong on both counts, and the correction is kept here so nobody
      re-derives it. The wedges appear identically in the on-screen chart
      at the default window size and predate any print work, and raising
      `iFillMax` to 4096 or 16384 leaves the printed output unchanged —
      measured, two prints differing only by the clock. The test that
      seemed to confirm the fill theory, setting Wheel Fill to None and
      watching them vanish, proved nothing: with no fill the whole
      backdrop is black, so an unfilled wedge can't be seen against it.
      Whatever they are, they are upstream rendering behaviour, not
      print-specific. Don't change `iFillMax` over them.

12. ~~The Qt build has no animation loop.~~ — **done 2026-08-25.** The
    Animate menu used to set `gs.nAnim`, `gi.nDir`, and the jump rate and
    factor exactly as Windows does, while nothing consumed any of it:
    `Animate()` ran only from the two Step items, and there was no
    `QTimer`, `startTimer`, or `timerEvent` anywhere in the Qt sources.
    A `QTimer` created in `BeginQt()` now runs for the whole session and
    checks the same guard Windows' `WM_TIMER` does
    (`gs.nAnim < 1 || gi.fPause`), then calls `Animate()` +
    `RecastAndRedrawQt()` — the same pair the Step items already used.
    - The interval lives in `s_nTimerDelay` (qtdriver.cpp), the Qt build's
      stand-in for Win32-only `wi.nTimerDelay`, default 100ms like
      Windows. That makes Graphics Settings' **"Update Delay in
      Milliseconds"** portable, so it is no longer skipped; `NAnimDelayQt()`
      and `SetAnimDelayQt()` are the accessors, and the setter retimes the
      running timer immediately.
    - Verified: with Jump Rate = Days the chart date advanced Sep 9 →
      Oct 12 2026 over three seconds (one day per tick), and Pause
      Animation held it at zero pixels changed.
    - **Worth knowing, and not a bug:** "Do Animation" does nothing from a
      cold start. It is `neg(gs.nAnim)`, and `gs.nAnim` defaults to 0, so
      negating it is a no-op — animation only arms once a Jump Rate has
      been picked (which assigns a positive value directly). Windows'
      `cmdAnimateNo` is the identical `neg(gs.nAnim)` with the identical
      default, so this matches upstream exactly. Don't "fix" it without
      deciding to diverge on purpose.

13. ~~Chart-type switches do nothing from the command line or a macro.~~
    — **done 2026-08-25.** `-Z` typed into the Command Line dialog, or run
    from a macro, set `us.fHorizon` but the chart kept drawing as a wheel.
    Command switches don't go through `SetChartModeQt()`, so nothing
    updated `gi.nMode` or the Chart menu.
    - **This is an improvement on Windows, not a parity fix.** Windows
      zeroes `gi.nMode` only in its pending-menu-mode path
      (wdriver.cpp:1148, inside `if (wi.nMode)`), and `FActionX()`
      re-derives it only when it is zero — so after a command switch
      Windows' chart doesn't change type either. Worse, its `RedoMenu()`
      *does* re-derive the menu radio, so its menu and its chart end up
      disagreeing. Deliberately not reproduced.
    - Fixed by snapshotting the chart-type flags before running the
      switches and routing whichever one they newly turned on through
      `SetChartModeQt()`, so flags, `gi.nMode` and the menu all agree.
      Snapshot-and-compare rather than deriving the mode from the flags
      afterwards, because a switch sets its own flag without clearing the
      previous mode's — after `-Z` from a wheel chart both `fListing` and
      `fHorizon` are true, and choosing between them by priority is
      guesswork. Which one is *newly* set isn't.
      `SnapChartModeQt()` / `SyncChartModeFromFlagsQt()` / `CChartModeQt()`
      in qtdriver.h; called from `ShowCommandLineDialogQt()` and
      `RunMacroQt()`.
    - **Gotcha #5 is now obsolete** — it warned that adding a chart mode
      needed two edits to `SetChartModeQt()` that could drift apart. The
      clear-list and the switch are now one `rgchartmodeQt[]` table of
      mode/flag pairs, so a new mode is one line in one place.
    - Verified all three paths: `-Z` from the command line switches to
      Local Horizon *and* moves the menu radio; a macro running `-L`
      switches to astrocartography; and picking Aspect Midpoint Grid from
      the menu still works after the refactor.

14. ~~The four dialogs outside the item-8 sweep~~ — **audited
    2026-08-25.** Chart List and Charts #3 through #6 turned out complete:
    every button, radio and field in `dlgList` and `dlgInfoAll` was
    already present, including Chart List's five sort options and its
    name/location filter, and the multi-chart dialog's four Progress
    checkboxes. Two real gaps in the other two:
    - **About was missing its entire credits and license block.** It
      showed a version line and a release date; Windows' `dlgAbout`
      carries authorship, both websites, the Swiss Ephemeris and Placalc
      attributions, the Neely/Erlewine formula credit, GeoNames, the TZ
      database, the PostScript credit, and the GPL notice — which says in
      terms that these notices "must not be changed or removed by any
      user or editor of the program". Restored verbatim, with URLs
      selectable and clickable. Also fixed the title of Charts #3
      through #6, which was just "Charts".
    - **The command line dialog was missing "Enable AstroExpression
      hooks"** (`dxCo_e`), Windows' checkbox for `us.fExpOff`, applied
      before the switches run so a line can be tried with expressions
      off. Added, and its prompt now uses Windows' wording ("Enter
      command switches below:") rather than this port's own.

15. ~~Graphics Settings' six font pickers~~ — **done 2026-08-25.**
    Previously left out on the grounds that they pick from a list of
    Windows GDI face names with no Linux equivalent. That was only half
    true: the fonts ship *with Astrolog*, in `font/`, and their internal
    family names match `rgszFontName[]` (xdata.cpp) exactly.
    - `LoadBundledFontsQt()` (qtdriver.cpp) registers the seven bundled
      `.ttf` files with `QFontDatabase::addApplicationFont()` at startup,
      looking beside the binary and then in the working directory, so
      they resolve without being installed system wide.
    - The pickers alone would have been decorative. Astrolog dispatches
      every sign, planet, aspect, house and Nakshatra glyph through
      `DrawGlyph()` (xgeneral.cpp) and text through `DrawSzFont()`, and
      both had PS, SVG and WINANY branches but no Qt one. Selecting a
      font therefore did nothing — worse than nothing, since `DrawSign()`
      and its siblings have already swapped the glyph for a character
      code meant for the chosen font, so the vector fallback drew that
      raw character. Both now have a `#ifdef QT` branch, and the five
      `DrawGlyph()` call sites were widened from `#ifdef WINANY` to
      `#if defined(WINANY) || defined(QT)`.
    - The six combos in Graphics Settings are filtered per category by
      `rgszFontAllow[]`, as Windows filters them — not every font carries
      glyphs for every category. File Settings' "Use Real System Fonts"
      master switch is in too, with the same `gs.nFontAll`/`gi.nFontPrev`
      packing Windows uses.
    - Verified: setting Signs to Enigma visibly changes the sign glyphs
      on the wheel. Both builds compile.

16. **Menu parity is measured, not asserted.** The test suite parses
    Windows' main menu resource (the `menu MENU` block in astrolog.rc)
    and checks every item against the live Qt menu bar: **258 of 258
    present**, plus 12 deliberately skipped (Setup submenu, Print Setup,
    wallpaper modes, Buffer Redraws) and the 96
    macro slots, which the hotkey test covers instead.
    - It matches ignoring `&` placement, since the two builds don't
      always put the mnemonic on the same letter and that isn't worth
      failing over. It *does* check each item sits under the same
      top-level menu, because putting one in the wrong menu is a real
      parity bug and has happened here before.
    - Rerun it after any menu change. The table is generated from
      astrolog.rc — regenerate rather than hand-editing if upstream's
      menu changes.
    - This replaced grepping qtdriver.cpp for label strings, which
      counted text inside comments and missed anything built at runtime.
      That method claimed "123 missing" where the truth was 4.
    - The four it found were all wording: this port had "Save Chart...",
      "Quit" and "Open Documentation..." where Windows has "Save Chart
      Info...", "Exit" and "Open Documentation". Now matched. **"Quit" to
      "Exit" is the one worth a second opinion** — Quit is the Linux
      convention, and parity was chosen over it deliberately.

17. **The mouse does things no menu item covers.** Windows' chart window
    handles the mouse in `NWndProc()` (wdriver.cpp:820-960), and none of it
    is reachable from a menu, so menu parity counting is blind to it. Now
    ported in `ChartCanvas`:
    - **Right button drag rotates and tilts** globes, spheres, polar and
      world maps, astro-cartography, local horizon, telescope and midpoint
      charts. Previously the only way to rotate was nudging via menu items.
    - **Alt+click on a world map relocates the chart** to that spot.
      Deliberately disabled on constellation and Mollweide maps, as on
      Windows -- their projections aren't a plain lat/long grid.
    - **Freehand scribbling**: click sets a pixel, Shift+click draws a line
      from the last point, Ctrl+click a rectangle, Ctrl+Shift+click an
      ellipse. A plain drag is treated as repeated Shift+clicks so it draws
      a continuous line; a deliberate Shift+click leaves the anchor put so
      several lines can fan out from one point.
    - Note this made the already-ported **Scribble Color** menu item
      reachable: it set a pen there was no way to draw with.
    - The context menu had to move with it. Windows pops it on button *down*
      for most charts but on button *up* for the rotatable ones, skipping it
      if the drag rotated anything. Qt's automatic ContextMenu event can't
      express that (X11 fires it on press, Windows on release), so the
      canvas sets `Qt::PreventContextMenu` and drives the menu from the
      mouse handlers. Change one of these and check the other.

18. **The window sizing model is no longer hardwired.** The canvas used to
    set `gs.xWin/yWin` from its own size on every paint, which is Windows'
    *default* (`wi.fWindowChart` on, `wi.fChartWindow` off) but was the only
    behavior available. The View / Window Settings submenu now exists, with
    both toggles and all five sizing commands.
    - The canvas lives in a `QScrollArea`. With "Window Resizes Chart" on it
      tracks the viewport, exactly as before. With it off the canvas is
      sized to the chart and real scrollbars appear when the chart is
      larger than the window.
    - That is why none of Windows' `wi.xScroll` / `gi.xOffset` panning
      arithmetic (xscreen.cpp:396) is ported: Qt scrolls the viewport
      itself, and gets mouse wheel and keyboard scrolling for free. The
      four Scroll commands just drive the scrollbars.
    - **"Buffer Redraws" is deliberately still absent.** It toggles whether
      Win32 draws through an off screen bitmap; Qt composites every widget
      off screen no matter what, so there is nothing for it to switch. An
      item that silently does nothing would be worse than not offering it.
      It is the only Window Settings item left unported.
    - Full Screen is Qt's `showFullScreen()`. Windows saves and restores the
      window rectangle by hand there and can fail outright, warning the user
      about permissions; that whole failure mode doesn't exist here.
    - Watch the accelerator case convention when adding hotkeys: in
      astrolog.rc a **lowercase** letter is the plain key and an
      **uppercase** letter means Shift. `b` is Show Border, `B` is Size
      Chart to Window (so, `Shift+B` in Qt). Getting this wrong produced a
      duplicate binding the hotkey test caught.

19. **Check for missing backend branches before blaming rendering.**
    `xgeneral.cpp`'s drawing primitives are written as a chain of
    `#ifdef X11 / #ifdef WINANY` alternatives. Two of them, **`DrawArc()`
    and `DrawEllipse2()`, had no Qt branch at all** and so silently drew
    *nothing* on screen -- no compiler warning, no crash, just no curves.
    - Every circle in every chart was missing. Worse, `DrawFill()` floods
      up against those circles, so with the boundary gone a single sign
      sector fill escaped and swallowed the whole chart in one flat color.
      That one omission was most of "the wheel looks terrible".
    - `DrawArc()` on Qt now draws the arc as the same series of line
      segments the bitmap file path uses, rather than getting a
      `QPainter::drawArc()` of its own. Two reasons: the curve then
      rasterizes identically on screen and in an exported chart, and it
      comes out *closed*, which a flood fill depends on.
    - A grep worth repeating after any upstream merge:

          for each Draw* in xgeneral.cpp, does it have #ifdef X11 or
          #ifdef WINANY but no #ifdef QT?

      Still outstanding by that test: **`DrawStar()`** (WINANY only).
      `DrawColor()` and `DrawClearScreen()` also lack one but are fine --
      their generic code already does the work for Qt.
    - Watch `QPainter` state. The first cut of the `DrawEllipse2()` branch
      left `Qt::NoPen` set, which made everything drawn afterwards vanish.
      Save and restore pen and brush.

20. **The wheel is an ellipse when the window isn't chart-square.** This is
    correct, and matches Windows: `gs.xWin` is the *whole* client width and
    the wheel code subtracts the sidebar from it (xcharts1.cpp:79), so a
    circular wheel needs a window `SIDESIZE` wider than it is tall.
    `SquareX()` adds that automatically, which is why the app opens at
    760x600 -- a 600x600 wheel plus a 160 sidebar. Don't "fix" an oval seen
    in a forced-square test window; check the natural startup size instead.

21. **The dialogs are transcribed from astrolog.rc, not rebuilt.**
    `tools/rc2qt.py` turns each `dlg*` block into a control table
    (`qtrcdlg.h`), and `RcBuildDialogQt()` lays it out through Windows' own
    unit conversion. **23 of the 24 dialogs** go through it, including
    Object Selections, which this fork added to `astrolog.rc` rather than
    hand building in Qt precisely so it would.
    - Regenerate with `python3 tools/rc2qt.py astrolog.rc > qtrcdlg.h`
      after any upstream resource change, then rebuild -- the makefiles
      list the header as a dependency now, which they didn't at first, and
      a stale object file makes a regeneration look like it did nothing.
    - `tools/rc_audit.py` reports any control a dialog builds that nothing
      wires up. It has caught real ones: Calculation Settings' "3D Houses"
      box, and Graphics Settings' "Don't Automatically Redraw Screen".
      Run it after adding a dialog.
    - **dlgAbort is the one not transcribed.** It is a modeless "printing,
      Cancel" window that exists to service GDI's abort procedure while
      spooling. Qt prints synchronously through QPrinter with no abort
      hook, so the dialog would appear and vanish with nothing to cancel.
    - Things the resource hides, all of which cost a debugging cycle:
      `CONTROL` puts its style flags before the geometry while `EDITTEXT`
      and `COMBOBOX` put them after; a control can span two lines with the
      geometry on the second; a `COMBOBOX`'s height is its dropped-down
      extent, not the closed control; and `CONTROL` is a checkbox or a
      radio button depending on its style.
    - Numbering is not uniform. Colour lists start at `dck00` while the
      edit fields beside them start at `deo01`; pairing them by the same
      index puts every colour one row below its object.
    - Which checkbox drives which setting is extracted from the
      `SetCheck`/`GetCheck` pairs in Windows' own dialog handlers rather
      than worked out here, so the two builds can't drift apart on it --
      including the boxes that show the *inverse* of their flag.

22. **The interface font is bundled.** Astrolog's dialogs are laid out in
    units of the dialog font's average character width, against MS Shell
    Dlg, so a font whose strings run wider per unit of average width won't
    fit the resource's boxes. Measured across all 630 pieces of text:
    DejaVu Sans overflows 168, DejaVu Sans **Condensed** 177 (narrower is
    not better proportioned), Liberation Sans 8. Liberation is bundled
    under the SIL OFL in `font/`, and only the *family* is set -- the point
    size stays the desktop's, so its scaling still applies.
    - Do not widen the base unit to make text fit. "Atlas City Coloring:"
      sits in a 35 unit box and would demand a base of 19 against a natural
      9, very nearly doubling every dialog. Shrinking the whole dialog font
      does nothing either, since the base unit derives from that same font.
      The control that overflows shrinks its own text, and a label that
      still doesn't fit wraps, which is what Windows' static text does.

23. **Menu mnemonics come from the `.rc`, not from the obvious letter.**
    Where the `&` sits in `astrolog.rc` decides which Alt-key reaches an
    item, and upstream frequently does not put it first: `Standard
    Radi&x`, `Ga&uquelin Sectors`, `Inf&luence`, `Ara&bic Parts`,
    `Nea&rest Cities`. This port had picked the obvious letter, so Alt+C
    then `x` — which a Windows user has in muscle memory — did nothing.
    155 label sites were rewritten to the `.rc` spelling.
    - It is not enough to fix the menu bar. The context menu tables and
      the hotkey table both *name menu items by label*, so all three have
      to agree; changing only the menu bar broke 37 previously passing
      assertions until the other two were rewritten to match.
    - `tools/rc_mnemonic_audit.py` checks all 850 label sites and is
      clean. It compares labels with the `&` and the `\t` accelerator
      stripped, so it only ever flags a mnemonic that *moved*, never a
      wording difference — which keeps it useful as labels evolve.
    - `Carter& P.Equat.` really does put its `&` before a space in the
      resource, so Windows shows no underline on that item. Transcribed
      as-is; it looks like a typo and is faithful.

24. **Two chart types render nothing with graphics on.** `gAspect`
    (Aspect List) and `gArabic` (Arabic Parts) have no `case` in
    `DrawChartX()`, so selecting either while `us.fGraphics` was set drew
    an empty window — the blank screen reported after Alt+L. Windows sets
    `us.fGraphics = fFalse` for exactly those two chart types and no
    others (`wdriver.cpp`, `cmdChartAspect` and `cmdChartArabic`); both
    now go through `AddChartModeTextAction()` to do the same.
    - The existing render test missed it because it drove
      `SetChartModeQt()` directly, which leaves `us.fGraphics` alone — so
      it only ever exercised whichever mode the suite happened to be in.
      It now also fires all 16 Chart menu actions with graphics
      deliberately on. **A pixel-blankness assertion did not catch this**
      (something still paints offscreen); asserting the actual invariant —
      that each item leaves `us.fGraphics` where Windows leaves it — does,
      and reads better besides.
    - The other half of the same report was the F1 macro crash:
      `PrintError()` ended in `Terminate()` on every non-Windows build, so
      a macro naming a file that lives on another machine took the session
      down instead of complaining. `TestBadInputQt()` covers it;
      reinstating the `Terminate()` makes the suite die mid-run and exit 1.

25. **Text chart layout was verified against Wine, not assumed.** All
    eight text chart types (radix, house wheel, aspect grid, calendar,
    influence, ephemeris, aspect list, midpoint list) were captured from
    the mingw build under Wine and rendered again in Qt with the same
    chart data. Layout is character-for-character identical: same column
    positions, same header, same values. The IBM box-drawing path that
    `=k` enables maps correctly too.
    - Both apparent differences turned out to be data, not rendering. The
      header wraps to a second line whenever a chart *name* is set
      (`charts1.cpp:91` emits the newline), and cusp values differed
      because that Wine instance had 3D houses on. Worth checking before
      concluding the renderer diverges.

26. **A modal warning can block the Windows build before it draws
    anything.** `SwissEnsurePath()` (calc.cpp) concatenates the
    executable's directory, the working directory, several environment
    variables and a compile-time directory into one `char[AS_MAXCH]` —
    256 bytes. From a long enough working directory that overflows, and
    Astrolog calls `PrintWarning()`, which on Windows is a **modal**
    `MessageBox` raised before the first chart is drawn. The app sits on
    it: menu bar painted, client area blank, keystrokes going to the
    dialog. Launch the Windows build with `-Wt` (`wi.fNoPopup`) for any
    automated run. Threshold observed: a 50-character working directory
    was fine, 70 was not.
    - Worth knowing generally — **any** `PrintWarning`/`PrintError` in the
      Windows build is modal and will hang an unattended run. The Qt port
      routes both through `PrintWarningQt()`, which honours `FNoPopupQt()`,
      so it has the same hazard. *(Corrected 2026-08-26 — this used to say
      "and the same escape". It doesn't: `-Wt` sets Win32-only
      `wi.fNoPopup`, and nothing on the Qt command line reaches
      `SetNoPopupQt()`, which only the test suite and File Settings' own
      checkbox call. An unattended Qt run has no way to turn the boxes off
      from outside. Worth adding if anyone needs it.)*
    - **The debugging lesson is the expensive part: look at the whole
      frame.** The dialog was in every capture from the first run. It went
      unseen through several wrong theories — a bad clone, path length in
      a file-open buffer, filesystem speed, focus, startup timing — purely
      because every crop was of the top-left corner where the chart text
      belongs, and a `MessageBox` is centred. Open the full image before
      theorising about why something rendered blank.
    - Two other wrong turns worth not repeating: three different md5s from
      the same source were read as evidence the binary differed, when the
      mingw build is simply nondeterministic; and "all eight captures are
      distinct" was accepted as "all eight captures are good", when
      distinctness says nothing about content.

27. **A warning raised before the window exists took the process down.**
    `astrolog-qt -i <file that can't be opened>` core dumped with
    `QWidget: Must construct a QApplication before a QWidget`, as did any
    switch given too few parameters. Backtrace: `main` →
    `FProcessSwitches` → `FInputData` → `FileOpen` → `PrintError` →
    `PrintWarningQt` → `QMessageBox` → `qFatal`.
    - **The ordering is the whole bug.** `main()` (astrolog.cpp) parses
      astrolog.as and then the command line well before `Action()` reaches
      `InteractQt()`, and `BeginQt()` — which constructs the QApplication —
      runs inside that. So the entire startup parse happens with no
      application object, and Qt refuses to build a widget there. Windows
      has no equivalent problem: `MessageBox(NULL, ...)` needs neither a
      window nor an app object, which is why this is Qt-only and why
      reading `wdialog.cpp` would never have suggested it.
    - Fixed in `PrintWarningQt()`: with `QApplication::instance() == NULL`
      it writes to stderr in the two formats the console builds use
      (general.cpp's `PrintWarning`/`PrintError`) and returns.
    - **`TestBadInputQt()` could not have caught this**, and its "survived
      missing files" line reads as though it should. It runs *inside* the
      program from `InteractQt()`, i.e. after the QApplication exists, so
      it exercises the opposite path. The check has to be a separate
      process, and now is: `run-qt-tests.sh` grew a **Startup
      diagnostics** section that runs the binary with a missing `-i` file
      and with a `-t` missing its parameters, and fails on a signal death
      or on that qFatal message. Confirmed to fail with the guard removed.
    - General lesson worth keeping: an in-process test suite cannot test
      its own process's startup. Anything before the event loop needs a
      subprocess check.

28. **Seven dialogs opened with focus on the wrong control.** Windows sets
    initial focus explicitly from `WM_INITDIALOG` — `SetFocus(GetDlgItem(
    hdlg, ...))` in `DlgCommand` (`deCo`), `DlgInfo` (`dcInMon`),
    `DlgDefault` (`dcDeDst`), `DlgTransit` (`dcTrMon`), `DlgProgress`
    (`dcPrMon`), `DlgList` (`dbLi_sl`) and `DlgGraphics` (`deGr_Xw_x`) —
    because the dialog manager would otherwise leave it on the first tab
    stop, which in every one of them is the OK button. Qt's default is the
    same, so it needed the same override; without it Enter opened the
    command line box and you had to Tab into the field before typing.
    - `FocusDialogQt()` (qtdialog.cpp) does it, called after
      `PrepareDialogQt()` in each of the seven.
    - Windows does *not* select the field's existing text when it does
      this — its handlers return `fFalse`, so no `EM_SETSEL` follows — and
      neither does this. The caret lands at the head, where
      `PrepareDialogQt()` already puts it.
    - Reported by the user, not by any test: nothing asserts focus. Worth
      knowing that the resource-driven audits can't see this either, since
      focus isn't in the `.rc` — it's in the dialog handlers.

29. **`-WM` leaked one allocation per renamed macro.** A settings file with
    thirteen `-WM` lines ended every session with upstream's own leak
    check firing: *"Number of memory allocations not freed before exiting:
    13"*. `NProcessSwitchesQt()`'s `M` case clones each name into
    `rgszMacroQt[]`/`rgszMSubQt[]` and nothing freed them.
    - Fixed with `FinalizeQt()` (qtdriver.cpp), called from a new `#ifdef
      QT` branch in `FinalizeProgram()` (astrolog.cpp) beside the existing
      `#ifdef X11` and `#ifdef WINANY` cleanup. That function is where a
      backend frees what it allocated through `PAllocate()`; the Qt build
      simply never had its branch. **Check it if you ever add a
      `PAllocate()` to the Qt sources** — there are none besides this one,
      which is why it went unnoticed.
    - The count matching the number of `-WM` lines exactly is what
      identified it. Upstream's check is a release-build feature, not a
      DEBUG one (only the companion "bytes not freed" line is `#ifdef
      DEBUG`), so users see it.
    - **Diagnosis note:** the first comparison run was wrong because it
      launched with no `-i` at all, and came back clean. The user's own
      repro — `-i <settings file>`, quit immediately — was the thing that
      reproduced. Take the reporter's exact invocation rather than an
      approximation of it.

30. **The arcs outside the wheel are a wheel-corner decoration, and are
    working correctly.** Large arcs sweeping through all four corners of
    the chart window, outside the wheel, turned out to be `-YXv 1 55` in
    the user's settings: decoration type 1, "Spider Web", at 55% size,
    drawn by `xcharts0.cpp:280-286` as a fan of straight lines from each
    corner. It reads as curves and as a moiré because of the fan, but
    type **2** is the actual "Moire Pattern" (`rgszWheelCorner[]`,
    wdialog.cpp:2732, mirrored in qtdialog.cpp). Shared upstream drawing
    code, identical on both backends. Nothing to fix.
    - Recorded because the wrong theory is instructive. The guess was that
      wheel fill being off (`-Xv 0`, also in that file) had stopped
      painting over them — the inverse of item 11's trap. That was
      plausible, tidy, connected to a real documented hazard, and wrong.
      The answer came from the user recognising their own setting, and
      then from grepping the settings file for what it actually enables.
      **Read the config before theorising about what a config produces.**
    - Real limitation found while trying to A/B it: **the Qt build cannot
      render a chart from the command line.** `-Xb` and the rest enter
      `InteractQt()` like everything else, so a chart only comes out
      through the GUI. The plan used to say such invocations "just hang";
      since item 27 they no longer crash, but there is still no headless
      export, which is what an image A/B or a pixel baseline would want.
      Carried up to "What to do next" as item 8.

31. **A resource change that doesn't rebuild the resource.** Adding the
    Object Selections dialog meant renumbering its control ids in
    `resource.h`. The Windows build came up with every value in the wrong
    column — Body showing the next row's object, names bleeding into the
    midpoint fields — which reads exactly like broken layout code, and
    was not: `Makefile.win` built `obj-win/astrolog.res` from
    `astrolog.rc` alone, with **no dependency on `resource.h`**. The
    compiled dialog template and the C++ that addressed it had been built
    against different id assignments. `resource.h` is a dependency now.
    - The same shape as the stale `qtrcdlg.h` warning in item 21, in a
      different makefile. If a dialog looks scrambled rather than wrong,
      suspect the build before the code.
    - Worth knowing that this failure is invisible to every audit here.
      `rc_audit.py` and `rc2qt.py` read the `.rc` and the header as text;
      neither can see what got linked.

32. **"It hangs" was three different things, and none of them was a bug.**
    Recorded because the wrong conclusion was stated confidently and had
    to be retracted.
    - `astrolog -i <saved settings> ` appeared to hang, and was reported
      as the console build failing to reload a file the GUIs read fine.
      It was not the file: **`astrolog -X` alone does the same**, because
      the console build *is* the X11 build, `=X` means graphics, and it
      opens a chart window and enters its event loop. That is the program
      working. With `DISPLAY` unset it says "Can't open display" and exits
      1; the window had been opening on a real desktop and sitting there.
      Force `_X` and the same file prints a chart and exits 0.
    - The user's own `astrolog.as` did not hang either. It exits 1 with
      **`Unknown switch '-WM'`**: `case 'W':` (astrolog.cpp) is guarded by
      `#if defined(WIN) || defined(QT)`, so the console build rejects the
      whole family, and per the comment there an unknown switch stops it
      reading the rest of the file. It never reaches the graphics switch.
    - **The lesson is narrow and worth keeping: an exit code of 124 from
      `timeout` says a process was still running, not that it was stuck.**
      Check what it is doing before calling it a hang. Two runs — one with
      `DISPLAY` unset, one with `_X` — would have settled it immediately.

33. **Driving a dialog by clicking needs the right origin.** Two OK clicks
    in a row missed, and the second one mattered: the chart compared
    identical before and after, which looked like a clean pass and was
    actually nothing having happened, because the dialog was still open.
    `xdotool getwindowgeometry` reported a position that was not the
    client origin; `xwininfo -id <id>` gave the true one and the click
    landed. **Always confirm the dialog actually closed before believing
    a before/after comparison**, and prefer `xwininfo` for absolute
    coordinates. This is the same family as the traps already listed under
    "Working pattern" and cost the same kind of time.

34. **The headless renderer already existed, and not knowing that cost
    an hour.** Item 9 of "What to do next" claimed the Qt build had no way
    to render a chart without the event loop. It does:
    `TextChartCaptureQt()` in qttest.cpp writes `gi.qim` to PNG with no
    display, and `QTTEXTDIR=out/qt ./run-qt-tests.sh` is how the Windows
    text chart comparison of item 25 was produced — in this same session,
    before the claim was written.
    - The cost was not the wrong sentence, it was what followed from it.
      Needing to compare two settings files semantically, and believing
      there was no headless render, the work went to the console build
      instead: which is the X11 build, so `=X` in a settings file opens a
      window (item 32), and which rejects `-WM` outright, so the config
      under test could not be loaded at all. Two dead ends, both avoidable.
    - Since fixed: `GraphicsChartCaptureQt()` does the other 24 modes, and
      `ProbeQt()` generalises the whole idea -- an empty function you
      rewrite to ask the program anything, ~0.2s a round trip. That, not a
      window driver, is how this program should be interrogated.
    - **The deeper miss was the axis, not the fact.** A window driver was
      built for questions that have nothing to do with windows. Every
      problem it then hit -- focus, coordinates, timing, modal dialogs,
      clicks that missed -- does not exist in a probe, because there is no
      window and no input. Check what the repo already does, and prefer
      asking the program directly over pretending to be a user.
    - The general lesson, and it is the same one as item 30: check what
      the repo already does before concluding it cannot do it. `grep -rn
      QTTEXTDIR` would have settled this in seconds.

## Features this fork adds to both builds

Everything else in this document is about reaching parity with Windows.
This section is the exception: work that goes *into* the Windows build as
well, because the user whose fork this is decided a good idea should not
be Linux only. It is deliberately shaped so it could be offered to
CruiserOne — no `#ifdef QT` anywhere in the shared or Windows files, and
the Swiss dependent parts guarded with `#ifdef SWISS` the way `DlgCustom`
already guards its own.

### Forced object positions are saved

`FOutputSettings()` (io.cpp) had no `-F`/`-Fm` section at all — the only
match for "force" in the whole file was a comment about `=XQ`. So File /
Save Program Settings wrote a settings file with every forced object
position missing, on Windows as much as here. A user with

    -Fm 19 1 2  ; POF = Sun/Moon midpoint

hand written in their `astrolog.as` lost it the first time they saved
settings from the GUI, silently.

This is upstream's defect and is kept as its own commit so it can be
offered separately. The section writes both forms, decoding what the
parse encoded: `force[i] = ZD(sign, deg) + rDegMax` for `-F`, and
`force[i] = -(o1*objMax + o2 + 1)` for `-Fm`. Degrees go through
`FormatR()` rather than a fixed `%f` so an exact value survives, and the
sign is written abbreviated, which `FMatchSz()` reads back on a three
character prefix.

**The loop covers every object from 0 to `cObj`, not the range any one
dialog shows, and that is the point.** A forced position can sit on
anything; a dialog showing a subset must never be able to drop the rest.
`TestForcedPositionsQt()` asserts exactly that.

### The Object Selections dialog

Setting / Object Selections (**Ctrl+T**), `dlgObjectSel` in astrolog.rc,
`DlgObjectSel` in wdialog.cpp, `ShowObjectSelDialogQt()` in qtdialog.cpp.
18 rows, one per Uranian and Dwarf slot, three columns and a button:

| column | is | backed by |
|---|---|---|
| Show | whether the slot appears in the chart | `ignore[]` |
| Contains | what the slot holds | `-Ye<x>` or `-Fm` |
| Name | what it is called | `szObjDisp[]`, i.e. `-YD` |

**Contains answers one question per row, and that is the point.** It first
had a Body column *and* a "Midpoint of"/"and" pair, which could both be
set, with the midpoint silently winning on position — a state no user
could be expected to infer. Now a row holds either a body or a midpoint,
and a midpoint is written the way astrologers write one:

    Nessus          a name from the offered list, or one already seen
    7066            an ephemeris number
    Sun/Moo         a midpoint, either half in any of these spellings
    7066/90482      the Nessus/Orcus midpoint
    h5              the raw -Ye definition text, as Object Customization

It is a task shaped view over two mechanisms that existed and had never
been reachable together: the `-Ye<x>` pair in `rgTypSwiss[]`/`rgObjSwiss[]`
that Object Customization edits as raw definition text, and `force[]`, the
`-Fm` switch, which no dialog on either platform had ever exposed. Show
sits beside them because selecting a body into a restricted slot changes
nothing visible, which otherwise reads as the dialog being broken.

Five things that are easy to get wrong later:

1. **The dialog's range is narrower than either switch's.** `-Yeb` reaches
   `custLo`..`custHi`, i.e. 34-83, the moons and body centers included,
   since `custLo` is `uranLo` and `custHi` is `cobHi`. `-Fm` reaches every
   object there is. The dialog shows 34-51 as a deliberate scoping. Every
   write is bounded to `uranLo`..`dwarfHi` and nothing is cleared
   wholesale, because `force[]`, `ignore[]` and `szObjDisp[]` are all full
   range globals being edited from a partial range dialog. A
   clear-then-refill loop would delete a forced midpoint on the Part of
   Fortune the first time someone opened this.
2. **A midpoint overrides the position but not the identity.** The force
   loop in `CastChart()` (calc.cpp) runs after positions are computed, so
   `-Fm` always wins for the same slot — but only for where it sits. The
   slot keeps its body, and so its glyph and its name. Hence the Name
   column, and hence Lookup Names offering `Sun/Moo` for a midpoint row.
3. **There is no name-to-number catalog, and there cannot be one.** A
   `.se1` file maps its own number to a name; nothing maps back, and the
   full collection is on the order of 887,000 files, so enumerating them
   at startup is not an option — it would stall for minutes. What is cheap
   is remembering: `ObjSelRemember()`/`FObjSelRecall()` (calc.cpp) record
   every name the ephemeris returns, so once the program has shown someone
   that 52872 is Okyrhoe, "Okyrhoe" is accepted back. Showing a name and
   then refusing it is indefensible, and that is the whole reason this
   exists. A fixed table rather than an allocated one, so it cannot grow
   without bound or reach the unfreed-allocation count at exit.
4. **`NParseSz()` matches `szObjName[]`, never `szObjDisp[]`.** So a slot
   renamed by `-YD` was refused under the name the dialog was itself
   displaying. `NObjSelMid()` covers all four spellings a midpoint operand
   can take: stock name, object index, a slot's display name, and an
   ephemeris number resolved to whichever slot holds that body. An index
   is read as an index before it is read as an ephemeris number, since
   that is what `-Fm` means by one.
5. **The definition parse exists once.** Windows open coded it twice and
   this port had a third copy; `FObjSelParse()` in calc.cpp is the single
   one, and it keeps Windows' `if (pch > sz)` guard. Without that guard an
   all alphabetic definition reads its own letters as flags, so `Ven` sets
   the north node off its own `n`. `TestObjSelParseQt()` fails if it is
   removed.

**Testing it needs the real config.** `-i nrvate.as` sets `-Yi1 "/swe"`,
and `SwissEnsurePath()` caches the ephemeris search path on first use, so
a `-Yi` set afterwards does nothing. Without it every esoteric body reads
`???` and the dialog looks broken when it is not.

### The -W settings this build stores are written back

*(Corrected 2026-08-26. This section first said ten switch families were
dropped and all were "equally pre existing". Two of them were not: they
were this fork's own gap, and they were the two that lost the most.)*

Running the real user config through both GUIs and diffing what came back
splits cleanly in two:

| switch | what it is | Qt saved | Windows saved |
|---|---|---|---|
| `-WM` | macro menu names, 13 in that config | **no** | yes |
| `-WN` | animation update delay | **no** | yes |
| `-Aa` | changed aspect angles | **no** | **no** |
| `-ao` | aspect list sort order | **no** | **no** |
| `-ma` | aspects to midpoints as well | **no** | **no** |
| `-YXv` | wheel corner decoration (the Spider Web of item 30) | **no** | **no** |
| `-YXa` | dashedness limit in aspect lines | **no** | **no** |
| `-Am`, `-Ao` | per-object and per-aspect maximum orbs | yes | yes |

**The first two were fixed.** `io.cpp` has the code that writes them, at
what is now the WINDOWS MENUS / WINDOWS DEFAULTS block, and it was
`#ifdef WIN`. So this build read `-WM`/`-WN` through
`NProcessSwitchesQt()` — which is why a user's macro names showed up in
the Edit menu — and then dropped them on every save, while Windows kept
them. The exact structural mirror of the `FinalizeQt()` bug in item 29:
half a Windows mechanism implemented.

There is now a `#ifdef QT` block beside the Windows one. **The rule it
follows is: write what this build can load.** `NProcessSwitchesQt()`
genuinely stores `-WM`, `-WM0`, `-WN`, `-Wx` and `-Wh`, so those are
written; it accepts `-Wn`, `-Wt` and `-Wb` as no-ops, so writing them
would claim a round trip that doesn't happen. Note `-Wx`'s own comment
there already said it kept the value "so it survives a settings round
trip", which it could not, because nothing wrote it.

**The other five were fixed too**, and are genuinely shared rather than
Qt only — neither build wrote them. `-Am` and `-Ao` turned out never to
have been lost at all: they set `rObjOrb[]`/`rAspOrb[]`, which the
`-YAm`/`-YAo` tables already write, so only the spelling differed. Check
the variable before believing a switch is dropped.

Two of the five are editable from a dialog, which made them the worst of
the set: aspect sort order in Chart Settings and the wheel decoration in
Graphics Settings could both be changed in the GUI and lost on save.

Two things worth knowing about the fix. `rAspAngle[]` had no defaults
array to compare against, so `rAspAngleDef[]` was added beside it in
data.cpp, the same shape `rgObjSwissDef[]` uses, and only changed angles
are written. And `us.nAspectSort` is an *index*, not the switch letter
that set it, so it maps back through `"jonOPACDM"` — writing `%c` of the
index puts a control character in the settings file.

**One trap if anyone takes those on.** Saved files load in the X11/console
build only because those lines are absent. `case 'W':` in astrolog.cpp is
guarded by `#if defined(WIN) || defined(QT)`, so the console build rejects
that whole family as an unknown switch — which, as the comment there says,
stops it reading the rest of the file. That is already true of the `-WM`
lines this change restores: a settings file saved from Qt or Windows is
not readable by the console build. Widening the guard is the other half of
the job, and item 32 is the story of mistaking that for a hang.

## Known divergences from Windows

Every place this port knowingly *differs* from Windows, so none of it
reads as an oversight later. Things the fork deliberately *adds* to both
builds are not divergences and live in their own section above. Anything
not on either list and not in an 8.x sub-item is unintentional — treat it
as a bug.

**Deliberately different behaviour**

| Where | Difference | Why |
|---|---|---|
| Display Settings | Raising "Number of Aspects to Include" actually un-restricts the newly included aspects | Windows assigns `us.nAsp = na` *before* the loop that would un-restrict them, so its loop can never run and raising the count silently does nothing. Kept the working version rather than reproduce the bug. |
| Chart info dialogs | Daylight shows and offers "Autodetect" | Windows resolves `dstAuto` via `DstReal()` before display, discarding the user's "work it out for me" choice on the next OK. Showing it survives a round trip. |
| Graphics Settings | Atlas City Coloring writes `gs.fLabelCity` | `DlgGraphics` writes `gs.fLabelAsp`, but that field is `-XA` (aspect glyphs on lines) and has nothing to do with city coloring. Treated as an upstream typo; using it would silently toggle aspect glyphs. |
| Command line dialog | Doesn't save/restore `us.fLoop`/`is.fMult` around the call | `CommandLineX()` does. Only matters for a typed line that itself starts a multi-chart sequence. |
| Restriction dialogs | (Since 8.9) checkbox = restricted, matching Windows | Previously "Show X" = visible, i.e. inverted. Flipped *toward* Windows, but it's a visible change to anyone used to the old Qt wording. |
| Menu accelerator column | Reads `Shift+V`, `Alt+L` where Windows reads `V`, `Alt+l` | Astrolog's resource writes an uppercase letter alone to mean Shift. Qt derives the column from `QKeySequence` and spells the modifier out. Same keys, different notation; changing it means overriding how Qt renders shortcuts. |

**Not ported**

| Thing | Why |
|---|---|
| `wi.*` fields in File/Graphics Settings — bitmap-from-window, antialias level, no-popup, no-auto-redraw | Win32-only `WI` struct. |
| Wingdings, and the plain text families (Arial, Courier New, Consolas, Lucida Console, Cascadia Mono) | Offered in the font pickers, but not bundled — Wingdings is proprietary and the rest are system fonts. Qt substitutes when absent, as Windows does. |
| Open Charts in Folder skips Astrolog's own data files | Matches Windows, which does the same, since a folder of charts often sits alongside them. |
| JPL Horizons lookup blocks the UI | Synchronous network fetch inside the modal dialog. Windows does the same. Obvious async candidate. |

## Explicitly out of scope (don't implement)

- View > Buffer Redraws (Qt composites off-screen regardless, so the
  toggle would switch nothing — the rest of Window Settings *is* ported,
  see item 18)
- File > Export as Wallpaper, all 5 modes (no portable Linux equivalent)
- Help > Setup submenu (Windows installer actions)
- File > Print Setup (native Windows print dialog)

This list is exactly the 12 items the parity test skips on purpose, so it
and `rgparityQt[]` in qttest.cpp have to agree — change one, change both.

(Right-click context menus used to be listed here. They aren't out of
scope and aren't outstanding either: all 42 are ported, see item 1.)

## Working pattern / verification methodology

- Build with `make -f Makefile.qt -j4`; binary is `./astrolog-qt`.
- Compile-check after every change before testing live — this codebase
  has caught real preprocessor/linkage mistakes this way every session.
- **Run the test suite before every commit.** `make -f Makefile.qt.test`
  then `./run-qt-tests.sh` — headless, no display needed, exits nonzero on
  failure. 2777 assertions covering dialog titles, the 42 context menus,
  264 shortcuts, 26 chart types rendering non-blank, all 338 menu items
  firing, 258/258 menu parity against `astrolog.rc`, and bad input. The
  suite runs inside the real program from `InteractQt()` after the window
  and menus are up, so it shares live `us`/`gs`/`gi` state — a test that
  changes a setting must put it back.
  - Grep build errors for `: error`, not `^qtdialog.cpp:` — a narrower
    pattern once hid a real failure in a generated header.
- **A test that passes both with and without the fix is worthless.** After
  writing a regression test, put the bug back and confirm the test fails,
  then restore. This project has produced two tests that were confirming
  an invention rather than catching a defect, and one assertion (pixel
  blankness) that looked reasonable and detected nothing. Asserting the
  actual invariant — "this item leaves `us.fGraphics` where Windows leaves
  it" — beat asserting a downstream symptom.
- **Generate from `astrolog.rc` rather than transcribing by hand.** The
  dialogs (`tools/rc2qt.py`), the 42 context menus and the 850 menu
  mnemonics were all derived from the resource script. Every time part of
  it was transcribed by hand instead, it introduced errors — wrong
  mnemonics on 155 labels, and four invented dialog titles that the tests
  then asserted. Where generation isn't practical, audit against the
  resource with a script (`tools/rc_audit.py`,
  `tools/rc_mnemonic_audit.py`) so drift is detectable rather than
  discovered by a user.
- **Build the Windows binary and look, rather than reasoning about what
  Windows does.** `make -f Makefile.win` cross-compiles the real thing
  with mingw-w64 — same `wdriver.cpp`, same `wdialog.cpp`, same
  `astrolog.rc` — and it runs under Wine. This has settled questions that
  code reading got wrong, and it is how the text chart layouts and the
  menu mnemonics were verified rather than guessed. **The workflow, and
  the headless-automation traps that go with it, are in
  `QT_COMPARING_WITH_WINDOWS.md`** — read it before driving either build
  with `xdotool`, because several of the failure modes there are silent
  and look like application bugs rather than harness bugs.
- **Prefer a toggle-free way to reach a known state.** Comparing the two
  builds went wrong first time because `v` is a *toggle*: pressing it
  leaves each build in whatever state it started in, and a graphics chart
  got compared against a text chart, which looks exactly like a rendering
  divergence. `wine ./astrolog.exe _X` starts in text mode
  deterministically, and the Qt side sets `us.fGraphics` in code. The
  general lesson: when a harness needs a known starting state, find the
  way to *assert* it rather than the way to flip it.
- **Answer questions with a probe, not by driving the UI.** `ProbeQt()` in
  qttest.cpp is an empty function whose body you rewrite freely: build,
  run with `ASTROLOG_QT_PROBE=1`, read the answer, rewrite it. About 0.2
  seconds a question, with every global live and no window, display,
  focus or coordinate anywhere in it. Driving a real window costs 60-240
  seconds a run and can miss. This was rediscovered the hard way after a
  session spent building a window driver for questions the probe answers
  instantly -- see item 34 and QT_TESTING.md.
- **Always run with `-i nrvate.as`.** It carries `-Yi1 "/swe"`, and
  `SwissEnsurePath()` caches the ephemeris path on first use, so a `-Yi`
  set after startup does nothing and every esoteric body reads `???`.
  A test against the stock astrolog.as answers a question nobody has.
- **Always verify new interactive behavior live**, not just by code
  review — this project has found multiple genuine pre-existing
  architectural bugs this way (`DrawDash()`'s invisible-solid-lines bug,
  the `gi.nMode` blank-chart bug, the `FBmpDrawMap()` world-map segfault,
  the Colored Text/Show Interpretations no-op bug) that code review alone
  would very plausibly have missed, since they only manifest at runtime.
  Reserve pure code-review confidence for dialogs that are a structural
  copy of an already-proven pattern (e.g. a new object-settings-shaped
  grid dialog when `ShowObjectDialogQt()`/`ShowAspectDialogQt()` already
  prove that exact shape works).
- GUI automation on a shared Linux Mint Cinnamon desktop is unreliable in
  specific, recurring ways: `xdotool getactivewindow`/`getwindowname` can
  reveal focus silently landed on an unrelated window — verify before
  sending keys/clicks. **Both input paths fail here, in different
  situations, so never assume an action landed** — re-check the mapped
  window list before screenshotting. Arrow keys inside an open popup have
  silently no-opped; and mouse clicks *on the menu bar* have silently
  done nothing when the WM placed the window on a secondary monitor, with
  the pointer provably over the right window. What worked reliably for
  menus: `xdotool key alt+<mnemonic>` to open, then `Down` xN + `Return`,
  remembering that Qt pre-highlights the first item (so N is index-1, and
  separators don't count) and that a modal dialog left open swallows the
  next `alt+` entirely, which looks exactly like the keyboard failing. **Never screenshot
  `-window root` or crop from it** — this has leaked unrelated desktop/
  window content into captures multiple times across sessions; always
  `import -window <specific-window-id>`, found via `xdotool search
  --pid <pid>` or `--onlyvisible --pid <pid>`, cross-checked with
  `xdotool getwindowname`/`xwininfo` before trusting an ID.
- **Measuring popup-menu item coordinates from a screenshot is genuinely
  error-prone** — the image you're shown may be displayed at a different
  apparent scale than its true pixel dimensions (check with `identify` on
  the actual PNG file, don't eyeball proportions from the rendered
  image). When repeated pixel-guess clicks miss, fall back to sampling
  actual pixel colors along a vertical strip (`convert img.png -crop
  1x<H>+<x>+0 txt:`) to find real row boundaries by color transition,
  rather than continuing to guess. After any click on a checkable menu
  item, verify you hit the right one (screenshot the menu again showing
  its checked state, or check window title after a dialog opens) before
  assuming success — several coordinate misses this project hit produced
  no error, just silently toggled the wrong thing.
- If a click's effect isn't visually obvious in a screenshot (e.g. window
  hide/show), verify with `xdotool search --onlyvisible --pid <pid>`
  rather than trusting a single screenshot — a screenshot of one window ID
  says nothing about whether a *different* window is still visible on top
  of or behind it.
- **Preserve line endings when editing upstream files.** Most of the
  original Astrolog sources are CRLF. Editing one through a script that
  reads and writes in text mode silently rewrites the whole file as LF,
  which makes it diff as entirely rewritten against upstream and would
  conflict across every line on a merge. This has happened four times in
  this project (`extern.h`, `io.cpp`, `xdata.cpp`, `xdevice.cpp`) before
  being caught. If you script an edit, read with `newline=''`, normalise
  to `\n` for matching, and convert back before writing. Check with
  `tr -cd '\r' < file | wc -c` against `git show HEAD:file | tr -cd
  '\r' | wc -c`. The fork's own files (`qtdriver.cpp`, `qtdialog.cpp`,
  `qtdriver.h`, `Makefile.qt`, the `.md` docs) are LF.
- **Scripted edits that compute a replacement range by index can eat the
  next thing.** One in this project deleted an entire adjacent plan item
  because its end index overshot, and it went unnoticed for several
  commits. Prefer exact-string replacement; if you must use a range,
  print what you're about to remove, and check the structure afterwards
  (`grep -c` the headings, compare line counts).
- **Verify a diagnosis before acting on it, especially before changing
  shared core.** Two confident wrong diagnoses in this project: the
  "print flood-fill artifact" that turned out to be normal on-screen
  rendering (the confirming test was invalid — with fill off, everything
  is black, so an unfilled region can't be seen), and a combo box read
  from a screenshot taken after wheel-scrolling, which the scroll itself
  had changed. In both cases the honest check was cheap: compare against
  a known-good baseline, or against the value in `astrolog.as` or the
  `GS`/`US` initialisers in xdata.cpp.
- For crash debugging: the release `Makefile.qt` build has no debug
  symbols. Build a throwaway debug variant (`sed` a copy swapping `-O` for
  `-O0 -g`, changing `OBJDIR`/`NAME` to avoid clobbering the release
  build), then `gdb -batch -x script.gdb --args ./astrolog-qt-debug` with
  a script doing `run`/`print <globals>`/`bt`, driving the crash-triggering
  click via `xdotool` in parallel. Delete the debug artifacts afterward —
  not gitignored, not meant to persist.
- Git hygiene for this repo specifically: never put a `Claude-Session:`
  line in commit messages here (existing history was scrubbed of it once
  already at the user's request). `Co-Authored-By:` is fine. Create new
  commits, don't amend. *(Corrected 2026-08-25 — this used to say the
  `qt` branch had never been pushed and that `origin` was upstream.
  Neither is true now.)* `origin` is the user's own fork,
  `git@github.com:nrvate/Astrolog.git` over SSH, and `qt` is pushed to it
  after each commit. `upstream` is `CruiserOne/Astrolog` with its push
  URL deliberately set to `DISABLED` — don't try to push there.
