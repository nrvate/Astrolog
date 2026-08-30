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
   display and no `xdotool`, and exits non-zero on failure. **3036
   assertions** as of 2026-08-29; it was 1396 when first written, and has
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

4. **~~Pixel-level baselines.~~** — **dropped 2026-08-27, measured
   rather than argued.** The idea was to hash each chart type's `gi.qim`
   and assert against stored baselines. It does not work here, and the
   claim that it would settle this project's rendering arguments does not
   survive checking either.
   - **The renders are not reproducible.** Two consecutive
     `QTGRAPHDIR` runs on one machine differ in **14 of 24** images, and
     not in a header timestamp: the changed region spans 692x473 of
     `wheel`, and most of `globe` and `worldmap`. Those charts are cast
     from the current moment, so planets move, the globe turns and the
     terminator shifts between runs seconds apart. Baselines would need
     the chart time pinned first, and would then still freeze font
     rendering and antialiasing that differ between the two machines this
     fork is worked on.
   - **It would not have caught any of the four rendering incidents that
     prompted it.** The moire arcs and the tick-ring wedges were "what is
     this?", which a baseline cannot answer; the blank Windows captures
     and the black client area were both Windows-side and both settled by
     *measurement* — all-images-identical, and "non-black rows 0..55 of
     1200" — which a Qt baseline never sees.
   - What already covers the catastrophic case is in the suite: 26 chart
     types asserted non-blank and correctly sized, which is what caught
     Aspect List and Arabic Parts drawing nothing. Above that line sits
     subtle rendering drift, which is the expensive, low-yield thing to
     chase. Measure a specific property when a specific question comes
     up, the way the sidebar width and the non-black row count were
     measured. Do not store pictures.
5. **Decide about the deliberate divergences.** The behaviours in
   "Known divergences from Windows" are places this port knowingly does
   something different. Five remain, and **four of them are cases where
   Windows looks like it has a bug** — Display Settings' aspect count that
   cannot un-restrict anything, Daylight discarding an "Autodetect"
   choice, Atlas City Coloring writing the aspect-glyph field, and the
   command line dialog's save/restore. Reproducing those for parity's
   sake would make the program worse; the recommendation is to keep them.
   The fifth is the restriction dialogs' checkbox sense, already flipped
   *toward* Windows.
   The one a user met on every menu — the accelerator column reading
   `Shift+V` where Windows writes `V` — **is fixed**, see item 44, so this
   item no longer has anything urgent in it.
6. **Unfinished business, low value:** Wingdings and the plain text
   fonts aren't bundled (see item 15) — a licensing fact rather than a
   task: Wingdings is proprietary and the rest are system fonts, and Qt
   substitutes when absent exactly as Windows does. The seven that can
   ship do.
   **The tick ring wedges are closed**, 2026-08-27, at the maintainer's
   direction rather than by being explained. Item 11 records what was
   ruled out — `iFillMax` for screen as well as print, and any
   fill-algorithm cause at all, since the two builds share no fill
   implementation — and that they could not be reproduced here under any
   fill mode, window size or decan setting, while the Windows build
   measured 34.9% and 26.2% background in the same bands. If they ever
   matter again, that is where to start, and measure the annulus by angle
   rather than cropping.
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
   **The last of it is fixed too**, 2026-08-27: `case 'W':` in
   astrolog.cpp used to be `#if defined(WIN) || defined(QT)`, so the
   console build rejected `-WM` as an unknown switch and stopped reading
   the rest of the file — a settings file saved from Qt or Windows was
   not loadable by it. The case is now unconditional, with a third branch
   for the GUI-less builds that consumes each `-W` switch's arguments and
   ignores it. Saved settings files are portable to every build.
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
5. **Adding a chart mode is one edit.** *(Revised 2026-08-25, and again
   2026-08-30 when the table moved to the shared core as `rgchartmode[]`
   in xscreen.cpp, item 111 — it now also feeds Windows' ProcessState()
   and DetectGraphicsChartMode().)* Add the pair to that table — above
   the `cchartmodeDetect` boundary, in priority position, if detection
   should find it; below otherwise — and `TestChartModeTableQt()`'s
   expected copy of the mapping, which will name the row until it
   matches. If the mode also wants a menu entry, add it with
   `AddChartModeAction()` as usual.
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

## Work log — items 1-80

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
    - **Looked at again 2026-08-27. Two more theories ruled out, and the
      wedges could not be reproduced at all.**
      - `iFillMax` is now ruled out for the *screen* as well, not just
        print: rebuilt at 65536 against the stock 255 and measured the
        black pixels in the sign ring annulus of the same pinned chart --
        16844 against 16900, which is noise. The earlier note only tested
        printed output.
      - **The two builds do not share a fill implementation**, so no
        fill-algorithm artifact can explain a shape both produce. Qt runs
        its own breadth-first flood fill over the QImage
        (xgeneral.cpp ~1278); Windows calls Win32 `ExtFloodFill` with
        `FLOODFILLSURFACE` (~1308). Whatever this is, it is above the
        fill.
      - **And at 900x900 with `-Xv 1` there were no wedges to find.**
        Sampling the sign ring annulus by angle at several radii came back
        **0% background** on both an unpinned chart and one pinned with
        `-qb 11 19 1971 ...`. So the artifact is not universal; it needs
        conditions not captured here, and the ones tried are not them.
      - **A caution about how it gets identified.** During this pass a
        crop of the ring was read as showing wedges at the sector
        boundaries, and the same reading was applied to a Wine capture of
        the Windows build -- before measurement showed the Qt ring was
        completely filled. The black band inside the coloured ring is the
        tick ring, which is *meant* to be black, and background outside
        the outer circle reads as a wedge at the right crop. **Measure the
        annulus by angle before believing an eye.** Next attempt: vary
        `gs.nDecaFill` (`-Xv 2`/`3`), window size, and `us.fListDecan`,
        measuring rather than cropping, and only then compare builds.

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
    - The user's own `astrolog.as` did not hang either. It exited 1 with
      **`Unknown switch '-WM'`**: `case 'W':` (astrolog.cpp) was guarded
      by `#if defined(WIN) || defined(QT)`, so the console build rejected
      the whole family, and per the comment there an unknown switch stops
      it reading the rest of the file. It never reached the graphics
      switch. Since fixed — see item 8 — so this particular reproduction
      no longer reproduces.
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

35. **A directory of 887,000 files is indistinguishable from a hang.**
    The Windows build given `nrvate.as` painted its menu bar, left the
    client area black and ignored every keystroke -- which is exactly the
    signature of the modal `PrintWarning()` dialog of item 25, except
    `-Wt` was already set and there was no dialog. The cause is
    `-Yi "/swe"`: the ephemeris collection is ~887,000 `.se1` files, which
    native Linux walks without complaint and Wine's path translation does
    not.
    - The tell that separates it from the dialog is a screenshot of the
      **centre** of the frame, because a message box is centred and chart
      content is not. Item 25 was missed for exactly the opposite reason,
      and this was nearly missed the same way -- every crop taken here was
      the top-left corner too, until the plan's own lesson was re-read.
      Better than looking: measure. "non-black rows 0..55 of 1200" states
      the symptom without anyone squinting at an image.
    - `tools/win-tests.sh` runs the real config with `/swe` swapped for
      the bundled `ephem/`. Everything that makes the config worth testing
      against survives -- restrictions, orbs, aspect set, macros, window
      size, graphics mode -- and only the file count goes. The Qt suite
      still runs the true path, where it costs nothing.
    - **Two confident wrong theories came first, both from changing two
      things at once.** That XTEST accelerators cannot reach Wine: they
      can, and `win-dialogs.txt` now drives three dialogs with `ctrl+t`,
      `shift+alt+a` and `alt+j`. And that graphics mode does not render
      under Wine: it does -- every prior capture used `_X` for reasons
      unrelated to rendering, so it had simply never been tried. Each was
      "confirmed" by a run that also differed in the config. One variable.
    - A retry loop was added to `windrive.sh` on the strength of the first
      theory. It stayed, because a mnemonic sent into a menu that never
      opened is silent either way, but its comment was rewritten: a
      comment that explains a symptom by a cause that turned out to be
      false is worse than no comment.

36. **Two long-standing shared-code bugs, both in upstream, both fixed
    2026-08-27.** Neither is a porting artifact; both are in files that
    have no `#ifdef QT` in them at all, so both builds had them.
    - **`:Xw` shrank the window by a sidebar on every save and reload.**
      `gs.xWin` includes the sidebar everywhere in this program: the code
      that wants the chart alone subtracts it and adds it back, and
      `FOutputSettings()` writes `:Xw` *without* it and says so in the
      comment beside the value. `NProcessSwitchesX()` (xscreen.cpp) read
      it back without adding it on again, so save, reload, save, reload
      walked a 1500 wide window down 1500, 1260, 1020, 780 -- 240 pixels
      a cycle, slow enough to read as imagination rather than a bug.
    - **`-Fm` between two restricted objects produced confident garbage.**
      A forced midpoint is computed in `CastChart()` from `planet[]` of
      its two sources, but `ComputeEphem()` skips any restricted object
      above the Moon, so the midpoint was built from whatever those slots
      last held -- a previous chart's values, or zero. `-Fm Kro Sun Moo`
      with Kronos' sources restricted read 0.0000 and looked like a real
      position. `FObjMidSource()` (calc.cpp) now reports whether any
      forced midpoint reads a given object, and both `ComputeEphem()` and
      the `AdjustRestrictions()` macro consult it: sources are computed,
      and stay hidden, because `ignore[]` still governs what is drawn.
      Restricting an object should hide it, not corrupt what depends on
      it.
    - Both were verified the way this file keeps recommending and the
      negative case caught a mistake: the first attempt at proving the
      midpoint fix replaced `!FObjMidSource(i)` with `fFalse`, which
      disables the skip rather than restoring the bug, so the test passed
      either way. `fTrue` is the restore. A test that passes with and
      without the fix is worthless, and it is easy to write one by
      accident.

37. **A once-only crash, chased with AddressSanitizer instead of
    guesswork.** The user hit a fault in the Object Selections dialog once
    and could not reproduce it. **Closed 2026-08-27 without a confirmed
    reproduction**, the maintainer's call, on the grounds that the
    stack-corruption fixed below is the likeliest cause and the dialog
    itself validates everything it is given. To be reopened if it recurs;
    what follows is what the hunt actually found, which was elsewhere. Reading the dialog code found nothing: the
    OK path validates both midpoint operands with `FItem()` before
    encoding them, `-Fm` validates the same way in astrolog.cpp, and
    `NObjSelMid()` returns -1 or a valid index and nothing else. So the
    crash was probably never in that code.
    - Building the suite under ASan settled it in one run. It is a good
      host: it already renders 26 chart types and fires all 338 menu
      items, so it exercises far more of the program in a minute than a
      person clicking could in an hour.
    - **`Makefile.qt.asan` already existed**, from item 24's three
      out-of-bounds fixes two days earlier, and this session rebuilt the
      whole setup by hand with variable overrides before noticing — then
      wrote it up that way, telling the next person no Makefile was
      needed. Item 34's lesson exactly, missed again. The sweep that
      should have caught it enumerated every `getenv()` knob and every
      `tools/` script against the docs and **never looked at Makefiles**;
      ASan appeared in no `.md` file at all, which is why grepping the
      docs could not find it. `QT_TESTING.md` documents it now, pointing
      at the Makefile.
    - **What it found was in intrpret.cpp, in upstream shared code, and
      had nothing to do with the dialog.** `ComputeInfluence()` guards its
      esoteric and hierarchical ruler lookups with `if (k)` in four
      places, where the block twenty lines above it correctly writes
      `if (k > 0 && i != k)`. `rgSignEso2[]` and `rgSignHie2[]` are almost
      entirely -1 -- only Leo has an esoteric co-ruler -- so `if (k)`
      passes -1 straight through and `power1[-1] +=` writes below a
      `real[objMax]` on the stack. It fires on any Esoteric chart with
      those rulerships on, which is to say routinely, and in a normal
      build it silently corrupts whatever local sits below the array.
    - **A second instance sits in `ChartInfluence()`, the `-j0` sign
      listing, and it is worse.** That loop iterates *objects*, but
      `rgSignEso1[]`/`rgSignHie1[]` are indexed by *sign* and hold 13
      entries, so any object above Pisces reads off the end of the table
      and then uses the result as an index. Confirmed with ASan through
      `ProbeQt()` rather than assumed from reading. The index and the
      value are both guarded now, which ends the undefined behaviour --
      **and the arithmetic is fixed too** -- see item 38, which turned
      out to need no new data at all.
    - **ASan stops at the first error, so a fix reveals the next one
      behind it.** Line 1342 hid line 1456 completely. Re-run after every
      fix until it is silent; one clean report is not evidence when the
      first error aborted the run.
    - **It also exposed a hang in the suite's own scaffolding.** The
      menu-firing loop closed modal dialogs with a single
      `QTimer::singleShot(120, ...)` and, unlike `StrOpenDialogQt()`, had
      no safety net behind it. Under ASan everything runs about ten times
      slower, the dialog appears after its own timer has already fired,
      and `trigger()` then waits forever for a click nobody is there to
      give -- twenty minutes of "it must be a slow build" before the
      symptom was recognised as the modal-dialog trap this file already
      documents. The timers now scale off `__SANITIZE_ADDRESS__`, which
      the compiler defines on its own, and the missing safety net is in.
      Diagnosed from outside exactly as the doc says to: `do_poll`, no
      sockets, 39 seconds of CPU against 19 minutes of wall clock.

38. **The table the `-j0` loop needed already existed.** Item 37 stopped
    `ChartInfluence()` corrupting the stack but left its arithmetic wrong,
    on the reasoning that computing what upstream meant would need an
    inverse of the sign keyed esoteric tables that nobody had written.
    Somebody had. `rgObjEso1[]`, `rgObjEso2[]`, `rgObjHie1[]` and
    `rgObjHie2[]` are in data.cpp and declared in extern.h, indexed by
    object and holding a sign, exactly as `ruler1[]`/`ruler2[]` are the
    object keyed counterpart of `rules[]`/`rules2[]`. The fix is to use
    them: the block becomes a copy of the traditional one directly above
    it, and reads correctly for every object rather than only for the
    twelve whose index happens to fall inside a sign table.
    - **That also explains where the bug came from.** The two directions
      spell "none" differently -- `0` in the object keyed tables, `-1` in
      the sign keyed ones -- so `if (ruler2[i])` is a correct test and
      `if (rgSignEso2[j])` is not. The esoteric block was written by
      copying the traditional one and swapping the table names for the
      only esoteric tables whose names were remembered.
    - The second block's `ignore7[rrEso]` is now `rrHie`. Hierarchical
      rulers had been applied whenever esoteric ones were enabled, and
      never on their own; the two flags are independent again.
    - **Third time in one session that something was rebuilt which the
      repo already had** -- after `Makefile.qt.asan` (item 37) and the
      headless renderer (item 34). Here the inverse tables were derived
      from scratch, generated into a fresh `data.cpp` block, and only
      then found to be duplicates, because the search was for a *concept*
      rather than a name. `grep -n "oNorm+1" data.cpp` lists every object
      keyed table in the program on one screen. When about to add data,
      look at what is already indexed the same way.
    - **The generated version was not wasted.** It agreed with the
      committed tables on all twelve rulers of both schemes and on three
      of the four "veiling" entries, which is a real cross-check of data
      nothing else here verifies -- and it disagreed on the fourth, which
      is now a question for the maintainer rather than an assumption:
      `rgObjEso2[oVul] = sVir` says Vulcan is veiled in Virgo, and
      `rgSignEso2[sVir]` is -1 and says nothing is. **Astrolog's own two
      tables contradict each other**, so the influence calculation credits
      Vulcan with Virgo while the interpretation text does not mention it.
      The object keyed entry is the one consistent with the source: Virgo's
      esoteric ruler is the Moon, and Bailey's recurring teaching, as
      transcribed in the astromcp repo's `esoteric.py`, is that "the moon
      is spoken of in the ancient teaching as veiling either Vulcan or
      Uranus". Not changed here, because it moves both a number and an
      interpretation, and because that repo carries the general teaching
      rather than a per sign table that would settle it outright.
    - **Cross-checked against an authoritative transcription.**
      `/shares/astromcp`'s `esoteric.py` carries the same three schemes
      read from Bailey's Tabulation VI and corroborated against
      Tabulations IV/V/VII and Oken's Tabulation 4. Astrolog's esoteric
      and hierarchical rulers match it **12/12 each**, and Astrolog keeps
      Vulcan and Vulkanus as separate objects, which is the confusion that
      repo carries a standing warning about. The data was never the
      problem; only the code reading it was.

39. **The Windows-versus-Qt audit: one real astrological gap, and it was
    a silent data loss.** The question asked was whether this port fails
    to implement anything Windows does that is not window specific. The
    way to answer it is not to read menus -- the suite already asserts
    258/258 of those -- but to look at shared code that branches on `WIN`
    with no `QT` branch, because that is where a feature can simply not
    exist here. `grep -nE "^#(if|ifdef).*\bWIN\b" *.cpp *.h` on
    everything except the two backends finds about 105 sites; discarding
    those with a `QT` branch within a dozen lines leaves roughly 40 to
    read.
    - **charts2.cpp had a matched pair, and this build had neither half.**
      `CastRelation()` cleared `us.nRel` after casting under `#ifndef WIN`,
      and restored `ciCore` from `ciSave` under `#ifdef WIN`. Both make
      sense: a console build casts once and exits, a GUI recasts the same
      chart over and over. This port is a GUI taking the console path.
    - **What that did to a user.** Time Space Midpoint is a composite
      chart, and `SetRelQt()` -- a faithful port of Windows' `SetRel()` --
      stashes the loaded chart in `ciSave` on entering that mode and puts
      it back on leaving. The restore is gated on `us.nRel` still reading
      `rcMidpoint`, which `CastRelation()` had just zeroed. So viewing a
      composite chart **replaced the loaded chart permanently**: enter the
      mode on a 2026 chart against a 2016 twin and it becomes 2021, leave
      the mode and it stays 2021. The chart the user opened is gone, and
      nothing says so.
    - **Fixing only the obvious half would have made it worse.** With
      `us.nRel` kept but the `ciSave` restore still Windows-only, every
      recast takes the midpoint *of the previous midpoint*: measured at
      2021, 2019, 2017, 2016, converging on the twin. A window resize
      would move the chart. Both guards are load-bearing and each was
      verified by disabling it alone.
    - `TestRelationshipModeQt()` covers all three properties -- the mode
      survives its cast, the chart does not drift across redraws, and
      leaving the mode restores what was loaded. Each assertion was made
      to fail on purpose, and the drift one needed the *persistence* fix
      left in to bite at all, since with the mode cleared there is no
      recast to drift.
    - **Everything else found was cosmetic or console flow**, and is
      listed under "Known divergences" rather than fixed silently. The
      only one with any user-visible edge is the text font family:
      charts1.cpp and general.cpp turn off IBM line drawing characters and
      change the degree glyph when `gs.nFontTxt > 0`, under `#ifdef WIN`.
      This port does implement font selection, so a text grid shown in a
      non-default font keeps line characters Windows would have dropped.
      Not changed, because whether it actually looks wrong is a question
      for a human at a screen, not for a code reading.
    - **AstroExpressions are the other asymmetry, and it is real but
      narrow.** express.cpp defines twelve functions under `#ifdef WIN`
      (`cfunW 12`): `Dlg`, `Mouse`, and ten readbacks of `-W` switch
      settings. Ten of those settings *do* exist in this build, so a
      script that reads `_WN` works on Windows and fails here as an
      unknown function. There is also `funWin` and `funX11` but no `funQt`,
      so an expression can identify every backend except this one.

40. **The pre-commit command was running the suite without the config.**
    Chasing why two builds of the same suite reported different assertion
    totals -- 2785 against 2847 -- turned up something worse than the
    discrepancy. `run-qt-tests.sh` passes `"$@"` through, so the bare
    `./run-qt-tests.sh` that CLAUDE.md gives as the check to run before
    every commit ran **without `-i nrvate.as`**, in the same file whose
    hard rules say always to pass it.
    - The cost was invisible by design. `TestObjSelTableQt()` skips any
      body whose name comes back `???`, on the reasoning that a missing
      ephemeris is not a wrong name -- and without the config
      `SwissEnsurePath()` never sees `/swe`, so **20 of 39 bodies resolved
      to nothing and 20 assertions skipped rather than failed**. A green
      run said 2781 and meant "half the esoteric bodies were not checked".
    - The runner now defaults to `-i nrvate.as` when given no arguments,
      and any argument still overrides it. 39 of 39 bodies resolve.
    - **A suite that only prints a grand total cannot answer "where?".**
      Two totals differing by 20 gave nothing to go on until per group
      counts were added, which named the group on the first run. Under
      `ASTROLOG_QT_TEST_VERBOSE` each group now reports its own count.
    - **The dialog counter in the menu test was measuring nothing.** It
      reported 143 dialogs in one build and 125 in another, and both were
      wrong: a queued close armed by one item fires a fixed delay later,
      lands inside a *different* item, and was recorded against that one.
      Text mode items that open no dialog at all -- Arabic Parts, the List
      tables, macros -- were being counted as opening one. Most dialogs
      here open asynchronously and appear after the trigger returns, so
      per item attribution is not available without restructuring, and the
      total is not stable run to run either (112-115, 126 under a
      sanitizer). The count is gone rather than dressed up; what the group
      actually proves -- every item fires, nothing hangs, the chart still
      draws -- is asserted, not counted. A number nobody can defend is
      worse than no number, because it gets quoted.
    - Fixing the counter did surface one real thing: the Arabic Parts menu
      item raises an **"Astrolog Warning"** dialog, which is now visible in
      verbose output rather than lost among 143 phantoms.

41. **Second Windows-versus-Qt pass, by a different question.** The first
    pass asked which shared code branches on `WIN` with no `QT` branch.
    Repeating that finds the same things, so this one asked instead: which
    *settings* do the Windows dialogs touch that the Qt dialogs never do?
    `grep -o "us\.[a-zA-Z_]*" wdialog.cpp | sort -u` against the same of
    qtdialog.cpp, minus anything the driver handles, leaves five.
    - **The ephemeris list offered what the user had switched off.**
      Windows builds that dropdown in a fixed order and omits entries the
      restrictions forbid: no JPL Web under `-0n`, no Placalc or Matrix
      under `-0p`. The Qt build ran the `cm*` constants in numeric order
      and added all seven unconditionally -- so with `nrvate.as`, which
      sets **both** restrictions, the list offered "JPL Horizons Web
      Query" to a user who had disabled web queries. It also listed JPL
      Web last where Windows lists it fourth. Both fixed; the selection is
      read back by name rather than index, so a shorter list needed
      nothing else.
    - **File restrictions were not honoured before the file picker.**
      `us.fNoRead`/`us.fNoWrite` appear nowhere in the Qt backend, while
      Windows tests them first and says "File input is disabled." The
      shared write path in io.cpp does honour `fNoWrite`, so nothing was
      actually written -- but only after the user picked a filename and
      was told nothing at all, which reads as a silent failure. Thirteen
      file dialogs now refuse up front, with Windows' wording.
    - **Writing the test for it exposed two leaks in the suite itself,
      and they were the reason it appeared not to work.** Every dialog
      check armed `QTimer::singleShot` closers that cannot be cancelled,
      so `TestDialogsQt()` left 25 pending and the menu loop left hundreds
      more -- and those went on to close the first modal window a *later*
      test opened. The new test saw no dialog at all and reported an empty
      list, which is worse than a failure: an empty string satisfies every
      "does not contain" assertion, so two of its four checks passed
      vacuously. Both sites now use timers scoped to the work that needs
      them -- stoppable ones in `StrOpenDialogQt()`, a single repeating
      closer for the menu loop -- and stop them before returning.
    - Diagnosing that took bisecting the test's position in the run: it
      passed first, failed after the dialog group, and the failure count
      changed depending on where it sat. **A test whose result depends on
      what ran before it is reporting on the suite, not the program.**
    - The lesson for writing these: assert that the thing being examined
      was found *before* asserting anything about its contents. The "found
      at all" check is what turned an empty capture into a visible failure
      rather than a green run.

42. **Third pass: the chart list ignored its AstroExpression filter.**
    The first two passes asked which shared code branches on `WIN`, and
    which settings the Windows dialogs touch that the Qt ones do not. This
    one asked which *shared functions* wdialog.cpp calls that qtdialog.cpp
    never does. Most of the 126 differences are Win32 helpers, and one
    cluster was not: `NParseExpression`, `ExpSetN`, and `us.szExpListF`,
    which had also turned up unexplained in the pass 2 list.
    - `DlgList` narrows the chart list three ways: by name substring, by
      location substring, and by AstroExpression. `RcFillChartListQt()`
      had the first two, faithfully, and silently lacked the third -- so
      an expression that narrowed the list on Windows did nothing here.
      Ported as written, including casting each candidate chart so the
      expression can see its positions and putting the real one back
      afterwards.
    - Verified both directions before writing a test, which mattered: an
      expression of `"0"` leaves "(No charts in list)" and one of `"1"`
      keeps all three, so the filter is being evaluated rather than always
      failing shut. `TestChartListFilterQt()` asserts both, and removing
      the ported block again fails the second.
    - **An angle that finds nothing is worth recording too.** The same
      pass compared settings *assigned* by wdriver.cpp against qtdriver.cpp
      and produced 42 apparent gaps, every one a false positive: the Qt
      build assigns those chart type flags through a table of pointers
      (`{gCalendar, &us.fCalendar}` and so on), which a search for
      `us.fCalendar =` cannot see. Check how a thing is written before
      concluding it is not.

43. **AstroExpression coverage, audited hook by hook.** Asked whether
    AstroExpressions are fully implemented here, the answer needed the
    list rather than an impression. There are **54 `szExp*` hooks**.
    Fifty-one fire from shared code and so behave identically in both
    builds. The other three each needed checking:
    - **`szExpListF`** (chart list filter) -- was Windows-only, ported in
      item 42.
    - **`szExpDisp3`** (notify after a redraw) -- Windows fires it at the
      end of its redraw; xscreen.cpp fires it too but from inside
      `#if !defined(WIN) && !defined(QT)`, so this build reached neither.
      Now fired at the end of `RedrawQt()`. `TestExpressionHooksQt()`
      asserts it fires and that `-~0` still suppresses it.
    - **`szExpKey`** (adjust a key press) -- fires only from that same
      X11-only block, so **Windows does not fire it either**. Not a parity
      gap, and not something to "fix" without deciding it is wanted.
    - **`szExpMenu`** (adjust a menu command before it runs) -- **done
      2026-08-27**, see item 45. It did need the label-to-command map and
      the dispatch reroute this entry called a design change; that turned
      out to be about forty lines and one scripted substitution.
    - Separately, **twelve expression *functions*** were `#ifdef WIN`
      (`cfunW`), and a `funQt` was missing beside `funWin`/`funX11`.
      **Both done 2026-08-27**, see item 46.

44. **~~The menu accelerator column~~** -- **fixed 2026-08-27**, and it
    was the only divergence a user met on every menu. Qt draws that column
    from the `QKeySequence`, spelling every modifier out, while
    astrolog.rc writes the string Windows draws verbatim after a `\t` and
    capitalises a letter to mean Shift. So this port read `Shift+V`,
    `Alt+Shift+V`, `Alt+L` where Windows reads `V`, `Alt+V`, `Alt+l`.
    - **A tab in a QAction's text is what Qt's menu painter checks first**,
      ahead of the shortcut, so carrying the resource's own string across
      replaces the rendering without touching what the key does. No
      override of Qt's shortcut handling was needed after all, which is
      what the old entry assumed would be required.
    - `tools/rc_accel.py` generates `qtrcaccel.h` from the menu bar block
      of astrolog.rc -- 269 items, the 96 macros excluded because they are
      renamed at runtime by `-WM` and their column is a plain function key
      Qt already spells Windows' way. Regenerating and diffing is a
      pre-commit check now, beside the other three.
    - The label stays the item's identity. Everything here finds an action
      by label, so both lookups compare only up to the tab, and so does
      the suite -- missing that turned `&Clear Screen` into
      `&Clear Screen\tDel` and four chart-rendering assertions failed
      before the cause was obvious.
    - **The apply pass had to match loosely.** A few dozen items carry the
      mnemonic on a different letter here than in the resource, on
      purpose, and they still want Windows' text.
    - Verified by opening the Chart menu on a private display and reading
      the column off the screenshot: `V`, `Alt+V`, `A`, `Alt+l`, `J`,
      `Alt+L`. Menus are one of the few things worth driving a real window
      for, and this is one of them.
    - **`qtdriver.o` had no dependency on the generated header**, so the
      first regeneration looked inert and the suite reported a count from
      a stale object file. Exactly item 31's trap in a new place; all
      three makefiles now declare it.

45. **`szExpMenu`: giving the port a command id to hand over.** Windows
    routes every menu choice through one `WM_COMMAND` switch and applies
    the `-~WQ` expression to the id *before* the switch, so a script can
    veto a command or swap it for another. This port binds each action to
    its own handler by label and had no id at dispatch, which is why the
    hook was unimplemented and why item 43 called fixing it a design
    change rather than a port.
    - It is a design change, but a small one. `tools/rc_cmd.py` generates
      the label-to-id map from the menu bar block of astrolog.rc and
      resource.h (366 items, every one resolving to a number), and
      `ConnectMenuQt()` replaces the bare `QObject::connect(pa,
      &QAction::triggered, ...)` at all 114 call sites -- a scripted
      substitution, since every site had the same shape. It looks the id
      up from the action's *own label*, so no call site needed a command
      constant, and it records each handler against its id so a
      substituted command can be dispatched to the right place.
    - Verified all three ways Windows behaves: an expression returning 0
      vetoes (mode stayed `gWheel`), returning another id runs that
      command instead (`40057` on the Grid item gave `gHouse`), and with
      no expression the item runs as itself (`gGrid`).
    - Actions whose label is not in the resource -- the runtime-renamed
      macros, the website links -- get a plain connection and simply do
      not take part, which is the same set the accelerator table skips.

46. **The twelve Windows-only expression functions, and a `funQt`.**
    `express.cpp` guarded `Dlg`, `Mouse` and ten readbacks of `-W`
    settings with `#ifdef WIN`, so a script reading `_WN` worked on
    Windows and failed here as an unknown function -- and `funWin` and
    `funX11` existed with nothing to identify this build.
    - Six of the ten settings map straight onto accessors this port
      already had (`NAnimDelayQt`, `FNoUpdateQt`, `FNoPopupQt`,
      `FBmpWindowQt`, `NAntialiasQt`, `FHourglassQt`). **Autosave and the
      screen saver report off**, because neither exists in this build; a
      readback that invented a value would be worse than one that says no.
    - `Dlg` and `Mouse` needed real work rather than a readback:
      `KvDialogQt()` puts up a `QColorDialog` and returns the chosen
      colour the way Windows' `KvDialog()` does, and `MousePosQt()` maps
      the cursor into canvas coordinates as Windows maps it into client
      ones.
    - Declared in extern.h rather than qtdriver.h, because that header
      pulls in Qt itself and express.cpp has no business including it.
    - The test reads settings back *after changing them* rather than
      asserting a constant: `_WN` follows `SetAnimDelayQt()` from 137 to
      42, `_Wt` follows `SetNoPopupQt()` both ways. An accessor wired to
      the wrong variable would pass a fixed-value check.
    - `cfunA` goes 484 to 485 for the new `QT` function, which is a shared
      count every build sees.

47. **Fourth parity pass: are controls wired to the *same* setting?**
    `tools/rc_audit.py` finds controls nothing wires up. It cannot see the
    quieter fault -- a control wired to the *wrong* variable -- which is
    what Windows' Graphics Settings does with Atlas City Coloring
    (`gs.fLabelAsp`, the aspect glyph setting), found by reading rather
    than by any tool. So: extract control-to-field from both backends and
    diff.
    - **65 controls checked, 0 mismatched.** The dialog wiring is right
      everywhere the two builds both bind one. `tools/rc_field_audit.py`
      keeps it that way and is a pre-commit check now; reversing one
      binding by hand makes it fail, which is how it was verified.
    - **The first version of it reported four mismatches, and all four
      were the script.** Both backends name controls the same way but
      reach them differently, and the index forms are the trap: a table
      row `{"dxSe_sr", 0, &us.fEquator2}` is control `dxSe_sr0` and the
      `-sr0` switch, `{"dxSe_sr", -1, ...}` is the bare name, and
      `PwRcFindIdxQt(rgbuilt, "dxMo_", 80)` is `dxMo_80`. Read as separate
      things they produce four confident false positives -- three of them
      pointing at the *adjacent* table row, which reads exactly like an
      off-by-one bug in the port. Every one was checked against the source
      before being believed, which is the only reason this entry says zero
      rather than four.
    - The five controls left unmatched are radio groups (`dr01`-`dr09`),
      which both builds handle by a different mechanism.

48. **Object Selections looked completely broken, and one line explained
    all of it.** Reported as: choosing an object does not set it, and
    Lookup Names does nothing with a number. Both builds, and both true
    from where the user sat.
    - **The slot was being set.** Driving the dialog from `ProbeQt()` and
      measuring rather than clicking: pick Chiron and `rgObjSwiss` goes
      7066 to 2060; type `52872` and it goes to 52872, with `planet[]`
      moving 352.0017 to 39.3855. The calculation had been right the whole
      time.
    - **The name never followed.** `szObjDisp[]` still read "Nessus", so
      every label in the chart named the old body at the new body's
      position. Nothing on screen changed except the numbers, which is
      indistinguishable from "it does not work" -- and worse than not
      working, because the chart was quietly mislabelled.
    - Now an untouched name follows the definition, and a name the user
      typed is kept. Telling them apart needs what the field held when the
      dialog opened, which both builds now remember.
    - **Lookup Names skipped any row that already had a name**, copied
      from `DlgCustom` where the name column is the user's own label. Here
      the button exists to turn a definition into a name, so it now
      resolves every row: type `52872`, press it, get "Okyrhoe".
    - Fixed in `wdialog.cpp` too, where the same two faults sat.
      `TestObjSelDialogQt()` drives the real dialog for all three cases and
      each assertion fails with its fault put back.
    - **Put through its paces afterwards rather than declared fixed**,
      since the first fix came from one reported symptom on one row. Six
      cases now drive the real dialog: picking from the list, a raw
      number plus Lookup Names, a hand-typed name that must survive, a row
      other than the first, a midpoint, Cancel, and an unparseable entry.
      - **Row 5 was the one worth checking.** An off-by-one in the row
        mapping would set the wrong slot, and every test above it used
        row 0, where an off-by-one is invisible. It is correct: row 5
        changes, its neighbours do not, and the Show box drives
        `ignore[]`.
      - **A midpoint had the same fault as the body selection**, found by
        this sweep rather than reported: typing "Sun/Moo" stored the force
        correctly and left the slot named "Hades", so it sat at the
        midpoint under the name of the body it used to be. Fixed the same
        way in both builds.
      - Cancel discards, and an unparseable definition applies nothing.
        Only the behaviour is asserted for the latter, not that a warning
        widget appeared -- see item 49, which is where the reason for that
        turned out not to be the reason first written down here.
    - **The lesson is about where the bug was looked for.** The feature
      was verified when built by checking that the settings it wrote were
      correct, and they were. Nobody checked what the dialog *showed
      afterwards*. A test that reads back the same variable the code just
      wrote proves the write, not the feature.

49. **"It sounds like the harness is broken" -- half right, and the half
    that was right mattered.** A test had been left asserting only
    behaviour, with a note saying a queued check "does not reliably run
    from inside the suite". That is the kind of claim worth testing rather
    than writing down.
    - **Queued timers were fine.** `TestTimerSanityQt()` arms a shot
      before a modal, and another inside that one during a second modal
      opened from it, and both fire. It stays in the suite, because every
      dialog test depends on that and nothing else checks it.
    - **What was actually broken was ordering**, and the proof was
      accidental: adding that two-assertion diagnostic *before* the Object
      Selections group made three of its assertions fail. The diagnostic's
      own `QTimer::singleShot` closers were still pending when the next
      dialog opened, and shut it.
    - Item 41 fixed exactly this in `StrOpenDialogQt()` and the menu loop,
      and then three test helpers written afterwards reintroduced it. The
      fix is now one place: `DriveModalQt()` waits for a dialog, runs a
      lambda against it, and stops both its timers before returning, so
      nothing it arms can reach a later test. **The suite has no
      uncancellable timer left in it.**
    - **Order-independence is now checked rather than assumed**: moving
      the four dialog groups to the front of the run gives the same 2847
      assertions and no failures. A suite that passes only in one order is
      reporting on itself.
    - The lesson is narrow and worth keeping: a note explaining why an
      assertion was weakened is a claim about the system, and this one was
      wrong. The user reading it as "the harness is broken" was a fairer
      reading than what it actually said.

50. **The dialogs ignored the desktop's dark mode, and Qt5 gives you
    nothing to fix it with.** Reported as "the dialogs are light on this
    machine, almost blinding", on Mint 22 / Cinnamon sitting on
    `Mint-L-Dark`. Two things had to be ruled out before anything was
    written, and both mattered.
    - **It is not `AstroStyleQt`.** `QApplication::setStyle()` is called
      at the top of `BeginQt()`, and Qt's docs are widely read as saying
      that resets the palette. Measured, in ten lines standing outside
      this program: the palette is byte-identical before and after the
      call. The custom style was innocent.
    - **It is not a missing package.** `qt5-gtk-platformtheme` was
      installed, and `QT_DEBUG_PLUGINS=1` shows `libqgtk3.so` being found
      and loaded. It loads and then supplies no palette at all — the app
      stays on Qt's default light `#efefef` while the desktop is dark.
      The **gtk2** plugin on the same machine reads the real theme
      (`#383838`). So whether a Qt5 app looks right on a GTK desktop comes
      down to whether `qt5-style-plugins` happens to be installed, which
      is not something to ask a user to know.
    - **Qt has the API, in Qt 6.5.** `QStyleHints::colorScheme()` is
      exactly the universal answer, and what it does internally is read
      the XDG desktop portal. That is out of reach here twice over: this
      builds against Qt 5.15, and the Qt6 packaged on the target LTS
      releases is 6.4, which is *below* 6.5. Requiring Qt6 would not have
      fixed the machine that reported it.
    - **So read the portal ourselves**, which is the same source of truth,
      then fall back per desktop: `org.freedesktop.appearance`
      `color-scheme` via the portal, then GNOME's `color-scheme` and the
      Cinnamon/GNOME/MATE theme name via `gsettings`, then XFCE's
      `xfconf-query`, then `kdeglobals`, then `gtk-3.0/settings.ini`, then
      `GTK_THEME`. The last three need no helper program, so detection
      still works on a machine with no glib tools at all. A
      `#if QT_VERSION >= 6.5` branch defers to Qt itself and compiles to
      nothing on Qt5. `ASTROLOG_QT_THEME=dark|light` overrides everything.
    - **Only synthesise a palette when nothing else has.** If a platform
      theme already produced a dark one it knows the real desktop colours,
      which beat anything invented here, so KDE and the gtk2 plugin are
      left alone. When we do paint, the style is pinned to Fusion, because
      it is the one bundled style that draws entirely from the palette —
      the GTK styles paint their own colours and would ignore all of it.
    - **`QSettings` cannot read `kdeglobals`, and that is not a typo.**
      The section is `[Colors:Window]`, and QSettings will not return a
      key out of a section whose name contains a colon: `allKeys()` lists
      `"Colors:Window/BackgroundNormal"` and handing that exact string
      back to `value()` yields an empty variant. `beginGroup()` and a
      percent-encoded key fail identically. Written with QSettings this
      compiles, runs, and silently reports every KDE desktop as light.
      Both file routes are hand-parsed now.
    - **The suite then caught a second QSettings trap**, which is the
      better argument for having written the test: it caches parsed files
      by timestamp and size, so rewriting `...prefer-dark-theme=1` to `=0`
      inside the same second reads back stale. Only reachable under test
      today, but it would be a live bug the moment anything re-reads.
    - Verified twelve ways, each route with the routes above it removed
      (`PATH=/nonexistent`, a scratch `HOME`): portal, gsettings, both
      file routes, both overrides, and the "nothing said" case. 26 new
      assertions; reintroducing the QSettings read fails two of them.

51. **Transit mode was a one-way door, and one shared `case` was why.**
    Reported from memory of the Windows build: Alt+Shift+N for a transit
    chart, then `c` to come back to a single chart -- and here `c` never
    came back.
    - **Windows runs two menu commands through one toggle.**
      `wdriver.cpp:1571` is `case cmdRelNo: case cmdRelComparison:
      SetRel(us.nRel ? rcNone : rcDual);`. Either item turns off a
      relationship chart of *any* kind, and turns comparison on when there
      isn't one. That is why astrolog.rc gives both items the same
      accelerator text `c` (lines 309-310) while the ACCELERATORS table
      binds `c` to `cmdRelComparison` alone.
    - **The port wired each item to its own fixed mode**, which reads
      correctly and is not. From `rcTransit`, `c` set `rcDual`; pressing it
      again set `rcDual` again. There is no other key for `rcNone`, so a
      user who pressed Alt+Shift+N could not get back to a single chart at
      all without the menu.
    - **Confirmed against the real Windows build under Wine**, since the
      claim was about remembered behaviour. Four screenshots through
      `tools/windrive.sh`: initial, Alt+Shift+N, `c`, `c`. The third is
      **pixel-identical to the first** (same MD5) and the fourth differs
      from both -- single chart, transit, single chart, comparison. The
      settings file records no `nRel`, so the chart itself is the only
      observable, and a single wheel against a bi-wheel is unambiguous.
    - **Asked whether this was a whole class of mis-wired toggles**, and
      it is not, but the checking is the point. Two sweeps: every
      `case cmd*` body in wdriver.cpp that branches on current state (22
      of 188 groups) read against its Qt counterpart -- Heliocentric,
      Solar Chart, Monochrome, Modify Chart, the six Include-category
      restrictions and the animation rate/factor sign handling are all
      faithful; and every `MENUITEM` accelerator in astrolog.rc grouped by
      key, looking for one key shown on two distinct commands, which is
      the signature of a shared toggle. **`c` is the only one in the whole
      resource.** A single bug, but now known to be single.
    - **A second, quieter gap in the same function.** `SetRelQt()` matched
      `rc` exactly to find the menu item to bullet, while Windows'
      `CmdFromRc()` (wdriver.cpp:251) bullets Comparison for *every*
      multi-wheel mode. `qtdialog.cpp:2126` reaches `rcTriWheel` through
      `rcHexaWheel` from the Charts #3 Through #6 dialog, and for those
      the loop found nothing and left the bullet wherever it was.
    - 13 new assertions. Each fix was reverted alone and fails exactly
      four of them, so neither is riding on the other.

52. **"Minors doesn't work properly with the toggles" was one line in a
    lookup helper, and it had mis-wired three dialogs.** Reported against
    the Object Restrictions dialog.
    - **What rc2qt.py does, and why it matters here.** It splits a trailing
      run of digits off a resource symbol into a separate index, so
      astrolog.rc's `dbRe_R0`, `dbRe_R1` and `dbRe_R` -- Restrict All,
      Unrestrict All, Toggle Minors -- all reach `qtrcdlg.h` as szId
      `"dbRe_R"` with nIdx 0, 1 and -1. A lookup by bare name means the
      one whose symbol carried no digits.
    - **`PwRcFindQt()` matched szId alone**, ignoring nIdx, so a bare
      lookup returned whichever the generated table listed first. For
      `dbRe_R` that is the nIdx=0 entry: **Toggle Minors was wired to
      nothing, and Restrict All got the minors toggle connected to it as a
      second slot.** Measured through the real dialog: Toggle Minors moved
      0 objects, and after Restrict All the minors read 0 restricted / 11
      not, because the button restricted everything and then toggled the
      minors straight back off. Cusps, Uranians and Dwarfs were fine --
      their symbols end in letters, so nothing collides.
    - **Asked whether it was a class, and this time it was.** Cross-
      referencing every bare lookup in qtdialog.cpp against qtrcdlg.h
      found seven symbols where a bare name sits beside indexed controls.
      Four happened to list the -1 entry first and were right by accident.
      **Three were wrong**: `dbRe_R` (Toggle Minors), `dbAs_RA` ("Toggle
      &Majors" in Aspect Settings, same shape, also dead), and `dxSe_sr`
      -- two checkboxes, where `us.fEquator` bound to the **"&Equatorial
      Latitudes"** box and "E&quatorial Longitudes" drove nothing at all.
    - **Fixed at the root**: a bare lookup now matches nIdx -1 too. Checked
      first that this breaks nothing -- all 146 bare lookups have a -1
      entry, so a strict match still resolves every one, corrects the
      three and leaves the four accidental passes alone.
    - **`rc_field_audit.py` could not have caught this, and uses `dxSe_sr`
      as its own worked example.** It compares the *tables* against
      wdialog.cpp, and the table was right: `{"dxSe_sr", -1, &us.fEquator}`
      is exactly what Windows does. The fault was one layer below, in what
      the lookup resolved that row to. Same shape as item 31: a check that
      reads source as text cannot see what got bound. `rc_lookup_audit.py`
      covers that layer now -- it fails if a bare lookup can find no
      control at all, and lists the 13 symbols where a bare name sits
      beside indexed ones. Confirmed non-vacuous by pointing a bare lookup
      at an indexed-only symbol and watching it exit 1.
    - 9 new assertions driving the three real dialogs. Reverting the one
      line fails four of them.

53. **Following item 52 up, and finding the audit was the thing that
    needed fixing.** Asked to fix the four symbols that item 52 had left
    "right by accident", and to take whatever else fell out.
    - **The four needed no fix, and saying so was the first job.** Once a
      bare lookup matches nIdx, `deCh_L`, `dxDi_Yu`, `dxGr_XQ` and
      `dxSe_Yn` resolve by construction, not by table order, and a
      reordered resource can no longer change the answer. The remark that
      closed item 52 -- that they were "one resource reordering away from
      mattering" -- was already false when written.
    - **What did need fixing was the new audit's scope.** It asked whether
      a bare-looked-up symbol had an nIdx -1 entry *anywhere in
      qtrcdlg.h*. That file holds all 24 dialogs concatenated and the
      symbols recur across them -- `dx01` and `deo01` belong to several --
      so the check passed on nearly anything. Rewritten to resolve each
      lookup against the controls of the dialog whose array the calling
      function actually builds: 208 lookups rather than 144, and it fails
      on a lookup matching zero controls *or more than one*. Confirmed non
      vacuous by injecting both a bad index and a bare lookup for an
      indexed-only symbol, and watching it name the function and exit 1.
    - **The scare was cross-dialog too.** A first pass counted 296
      duplicate (szId, nIdx) pairs and 283 of them looked up, which reads
      like a disaster. It is an artefact of the same mistake: within a
      single dialog there are **zero** duplicate pairs, so both lookups
      are unambiguous. The per-dialog list of symbols where a bare name
      sits beside an indexed one is seven, not the thirteen the table-wide
      pass reported.
    - **The four are now covered, and what that coverage is worth is
      worth being exact about.** Reverting the lookup fix does *not* fail
      them -- their tables happen to list the bare entry first, which is
      what made them accidental passes to begin with. Swapping two wiring
      rows does fail four of them. So they are field-wiring assertions,
      pinning which box drives which setting in four dialogs nothing else
      opened, and not a second regression test for item 52's bug. Calling
      them one would misrepresent what they check.

54. **Animation started itself, and the cause was a number in a struct
    initialiser.** Reported as: pressing a jump-rate key like `#` (Hours)
    starts the animation on its own, which Windows does not do.
    - **`gs.nAnim`'s sign is the on/off switch and its magnitude the
      rate.** So the rate items multiply by the sign to preserve it:
      `gs.nAnim = (gs.nAnim < 0 ? -1 : 1) * rate` (wdriver.cpp:2302), and
      the port copies that exactly. It only works if the value starts
      negative -- and `xdata.cpp` initialised it `-10` under `WIN` and `0`
      everywhere else. This port is neither, so it started at 0, every
      `(0 < 0 ? -1 : 1)` came out `+1`, and the first rate picked turned
      animation on. `neg(0)` is also 0, so `N` (Do Animation) did nothing
      from a standing start either. One `#ifdef WIN` with no `QT` branch,
      exactly the class item 39 swept for, in the one shape that sweep did
      not look at: a plain value in an initialiser rather than a code
      branch.
    - **Two Wine runs were worthless before one was worth anything.** The
      first pressed `#` and watched the chart: no movement, which looked
      like proof. It was not -- ASCII accelerators (`#`, `!`, and the rest
      of that row) never reach the Windows build through xdotool at all,
      as `numbersign` and `shift+3` both showed, while `shift+n` landed
      fine. Checking the Jump Rate bullet before and after found it
      **byte-identical**: the key had simply not arrived, and the test had
      measured nothing. Driving `cmdAnimateNo3` through the menu instead
      moved the bullet *and* left the chart still for eight seconds, which
      is the actual evidence. **Confirm the input landed before believing
      a before/after comparison** -- the same lesson as item 33, in a new
      place.
    - **The arithmetic is what caught the lie.** With `nAnim` at 0, `N`
      does `neg(0)` and cannot start anything, yet the first Wine run had
      shown it starting. Two observations that cannot both be true meant a
      premise was wrong, and the premise was the assumed default. That is
      what sent the search to the initialiser.
    - **Then two follow-up reports, and neither was a bug.** With the fix
      in, `p` appeared to do nothing and `r` started the animation. Both
      are Windows' own behaviour, measured from a standing start on Wine:
      Pause toggles `gi.fPause`, which is invisible while nothing is
      running, and Reverse Direction turns animation on when it was off
      (`if (gs.nAnim < 0) neg(gs.nAnim)`, wdriver.cpp:2323). The master
      on/off is **Shift+N**, not `p`. Left alone; parity is the spec, and
      a divergence here is the user's call rather than a fix.
    - 16 new assertions walking the whole sequence, including one on the
      startup value captured before any group can move it. Reverting the
      initialiser fails it.

55. **Four animation complaints, one bad encoding, and the first refactor
    that stopped being a port.** Item 54 fixed the startup default and the
    user came back with: `p` still does nothing, `r` starts the animation,
    and "how am I supposed to start/stop animation?" -- then, fairly,
    "there was no change in behavior from your last change", which was
    true: that commit was tests and documentation only.
    - **The answer to the question was Shift+N**, and having to answer it
      at all was the bug. `gs.nAnim` holds the rate in its magnitude and
      the running state in its sign; `gi.fPause` is a *second*,
      independent stop. So there were two ways to be stopped, two menu
      items that each moved only one of them, a rate control that started
      the chart as a side effect of arithmetic, and a direction control
      that started it on purpose.
    - **Six call sites open-coded the encoding** -- `(gs.nAnim < 0 ? -1 :
      1) * x` and `neg(gs.nAnim)` -- and getting the sign wrong in any one
      of them is invisible until something moves that shouldn't. Three of
      this port's animation bugs were exactly that. The encoding cannot be
      replaced (the `-Xn` switch and saved settings both read it), but it
      does not have to be repeated: it is now stated once behind
      `FAnimRunningQt()`, `SetAnimRunningQt()`, `SetAnimRateQt()` and
      `NAnimRateQt()`, and no other line in qtdriver.cpp reasons about a
      sign.
    - **The user's own framing settled the design**: "why do we need a
      toggle? its not a rocket we dont have to fucking arm it." One state,
      one control. `p` starts and stops. Rate selection picks a rate.
      Reverse reverses. Do Animation is the same switch under the Windows
      name the menu audit requires.
    - **This is the first deliberate behavioural divergence in the port
      that is not a limitation** -- everything under "Known divergences"
      before it was something Windows does that this build cannot or
      should not. Recorded there in full, on the user's explicit call that
      parity is not worth this.
    - 10 of the group's assertions fail if the old two-flag design comes
      back, which is a stronger regression net than the count suggests:
      they cover the states, not the keystrokes.

56. **Dialog mnemonics needed Alt here and not on Windows.** Reported
    against the restriction dialogs: "S=Sun, y=Mercury" used to work by
    themselves, and this port wanted Alt held.
    - A Win32 dialog acts on a mnemonic letter pressed alone; Qt only
      binds `Alt`+letter. Both builds take the mnemonics from the same `&`
      in astrolog.rc -- `rc_mnemonic_audit.py` already checks all 850 of
      them -- so nothing was missing, only the routing. On a grid of 52
      checkboxes that is the difference between the dialog being usable
      from the keyboard and not.
    - `MnemonicKeysQt` is an event filter on the dialog itself, so it sees
      the key only after the focused widget has declined it. A control
      that takes typing therefore keeps its letters: a `QLineEdit`,
      spin box, text edit or editable combo is checked for first, or
      typing a chart name into Set Chart Info would tick boxes across the
      dialog. Duplicated mnemonics cycle rather than always firing the
      first, which is what Windows does.
    - **It went in the wrong place first.** `PrepareDialogQt()` looked
      like the shared hook -- 21 dialogs call it -- but the restriction
      dialogs are not among them, and they were the ones reported. Moved
      to `RcBuildDialogQt()`, which every dialog built from the resource
      goes through. The probe said "NO EFFECT" eight times before the
      move, which is the only reason it was caught rather than shipped.
    - **The other half of the report took asking to pin down**, and was
      real. Tab order is exactly the resource order and is fine; the fault
      was the *arrow* keys. Qt moves focus on an arrow by walking the tab
      chain, Object Restrictions lists OK and Cancel before all 52
      checkboxes, and focus starts on OK -- so Up wrapped to the end of
      the chain and landed on "Recall" in the opposite corner, with Cancel
      sitting directly above. The first measurement missed it by starting
      from a checkbox in the middle of a column, where walking the chain
      and following the layout happen to agree. **Starting a measurement
      from the case the user actually described would have found it
      immediately.**
    - Arrows now pick the nearest focusable control in the direction
      pressed, scored as distance ahead plus four times the sideways
      drift. Down and Up walk a column, Left and Right cross between
      them, and Up from OK reaches Cancel. Controls that need the arrows
      themselves -- combos, lists, text fields -- keep them.
    - 33 new assertions: nine fail without the mnemonic filter, eleven
      without the arrow navigation.

57. **Lookup Names left half the row behind, and the test kept measuring
    the leftovers.** Reported: the button fills the Name box but the body
    field still reads a bare catalogue number.
    - The fix itself is small -- write `10199 Chariklo` into the body
      field too -- but the parse had to accept it first. A trailing run of
      letters after a space was always taken for point/flag letters, so
      `Chariklo` set the apsis marker off its own `a`. Measured as
      `pnt=4` by reverting the guard afterwards, not assumed.
      `FObjSelFlagRun()` requires every letter in the run to be a suffix
      letter, so `10199 nH` still reads its flags and `10199 Chariklo nH`
      reads both.
    - **The interesting part was the test, which failed three times for
      three different reasons, none of them the change.** It renders the
      wheel twice with different names and asks whether the picture moved.
      First `gs.fLabel` was off, so `DrawObject()` returned before drawing
      and every hash matched. Then `us.nRel` was `rcProgress`, where the
      wheel draws two charts and the forced slot is not the one labelled.
      Fixing those one at a time was the mistake: **the third attempt
      dumped 27 globals in a solo run and in a full run and diffed them**,
      which showed the real scale of it -- heliocentric, sidereal,
      equatorial, 3D houses, an Indian wheel, house system 22, monochrome
      and double scale, all inherited.
    - **`TestAllMenuActionsQt()` is why.** It fires all 338 menu items to
      prove none of them crashes, which is worth doing and leaves the
      program wherever that lands. Nothing after it can assume a setting.
      The test now copies `us` and `gs`, sets the baseline it needs on
      top, and puts both back at the end.
    - **The lesson is about method, not about the flags.** Guessing one
      variable per rebuild took three rounds and would have taken more;
      diffing the whole state against a known-good run took one and
      answered it completely. When a test passes alone and fails in the
      suite, dump state and diff rather than reasoning about which single
      thing it might be.
    - 13 new assertions. Each of the three changes was reverted alone:
      the glyph fix fails one, the parse guard two, the body-field write
      one.

58. **The definition parse existed four times, and only one copy had the
    guard.** The string cleanup's first real cut, after `FObjDefParse()`
    was split out of `FObjSelParse()`: `ParseCustomDefQt()` in
    qtdialog.cpp was a full copy, and Windows' `DlgCustom` open-coded it
    **twice more** -- once in its Lookup Names handler, once in its OK
    apply. All three retired against the one shared function.
    - **The drift was live in both builds.** Only the shared copy had the
      `FObjSelFlagRun()` guard from item 57, so a definition carrying a
      name beside its number -- `10199 Chariklo`, the very pair Lookup
      Names writes into Object Selections -- read the name's `a` as the
      apsis marker in the Custom Objects dialog and stored `nPnt=4`.
      Confirmed by reverting the Qt wiring alone: the new test then fails
      with exactly `nPnt 4`.
    - An empty definition field used to fall through the parse as "object
      zero"; it now reads as an invalid object, so the dialogs' own
      validation message fires instead of quietly storing Earth.
    - **The test-finding trap:** dlgCustom lays its 50 rows in two banks,
      so the edits sit in four x-columns (40/85/170/215). "Rightmost
      column, top row" picked row 25 of the wrong bank, and the store
      assertion caught the edit landing nowhere. Row zero's definition is
      the *second* column's top. The whole diagnose-fix-verify loop was
      four sub-second `ASTROLOG_QT_TESTS=custom-parse` runs.
    - Still open in this family: `DlgCustom`'s `WM_INITDIALOG` open-codes
      the definition *formatter* the same way (a near-copy of
      `SzObjSelDef()` minus the name shortcut), the Qt lookup's
      type-to-name switch duplicates `SzObjSelName()` except for the JPL
      Horizons web case, and the Custom Objects dialogs do not reset a
      redefined slot's glyph the way Object Selections now does.

59. **The formatter followed the parser into one function, and ASan then
    caught the suite's real intermittent crasher.** `SzObjDefFormat()` is
    the inverse of `FObjDefParse()` and existed three times -- inside
    `SzObjSelDef()`, and open coded in both Custom Objects dialogs'
    field pre-fills. One copy now, plus `ObjDefGet()` -- the read half of
    the planned Stage 2 accessor, arriving because all three sites began
    by gathering the four arrays. Ten round-trip assertions pin
    format-then-parse as the identity across every definition type, with
    and without suffixes.
    - **The full run then segfaulted, and the crash point was a lie
      twice over.** The output named a wireframe save; stdout was block
      buffered into the log, so the crash ate every group header and the
      last visible line was noise. Under gdb it passed outright. One
      ASan run replaced all of that guessing with line numbers.
    - **Three heap bugs in the suite itself, none from this session's
      code.** `TestChartListFilterQt()` sprintf'd twenty bytes through
      `ciCore.nam` -- a *pointer* into the one-byte "" cloned from
      nrvate.as, not a buffer -- smashing the neighbouring allocation on
      every full run since it was written. The text-capture path wrote
      NULs through the same shared clones. And `TestObjSelDialogQt()`
      saved `szObjDisp[iobj]` as a pointer and planted it back after the
      dialog's apply had freed it, leaving the global aimed at freed
      memory for the rest of the run and freed a second time at exit --
      **the suite's long-standing intermittent exit crash**. The glyph
      test's identical bug, fixed earlier the same day, had been a second
      independent copy; fixing it "resolved" the symptom only until the
      heap re-shuffled.
    - The lesson consolidates item 37's: an intermittent crash whose
      backtrace moves between runs is heap corruption, and the move is
      one ASan run, not another timing experiment. And `CI.nam` is a
      pointer that usually aims at a shared clone -- never write through
      it; point it at your own buffer.
    - The suite is now clean under ASan end to end, which it has never
      been before.

60. **The name lookup joined the parse and the formatter, and Custom
    Objects stopped hoarding stale glyphs.** Two loose ends from items
    58-59, closed together.
    - `SzObjSelName()` now knows definition type 4 -- a JPL Horizons
      body, named by asking JPL over the network, the way Windows'
      DlgCustom always had privately. Before, type 4 fell into the
      object-index branch, so a Lookup Names on `j2` in Object Selections
      answered **"Moon"**. Both Custom Objects lookup switches collapsed
      into the one function, which also means every name a lookup shows
      is now remembered and accepted back (`ObjSelRemember()`), which the
      open-coded switches never did. The type 4 path stays deliberately
      untested: JPLWEB is compiled in, and a suite must not do
      synchronous network I/O.
    - Both Custom Objects dialogs now drop a redefined slot's glyph via
      `SetObjGlyphNoneCore()`, the rule `-Ye` and Object Selections
      already follow; they were the last path that could leave Vulcan's
      glyph on a point everything else calls by its new name. Proven on
      row 1 (row 0 is already redefined by nrvate.as, so its glyph is the
      sentinel before the test starts and proves nothing): without the
      fix the assertion catches the slot still holding Cupido's raw
      turtle string.
    - `DlgCustom` is down to two locals under SWISS from six; the
      duplicated parse, formatter, and name switch were their only users.

61. **force[]'s three-states-in-one-real encoding is stated once, and the
    proof of it exposed a makefile hole.** Stage 1 of the object plan:
    FForceNone/FForcePos/FForceMid, ForcePos/RForcePos, ForceMid and
    ObjForceMid1/2 in extern.h beside the array, value-based so they
    serve both `force[]` and the dialogs' local `rgforce[]` copies.
    Fourteen open-coded pack/unpack sites, five rDegMax bias sites and
    the tag tests across seven files now read in words; `fun_F` alone
    still hands the raw encoded value to AstroExpressions, on purpose.
    Edge assertions pin the format: the largest legal midpoint pair
    unpacks to itself, and 0 Aries is a forced position rather than "no
    force".
    - **The load-bearing check then passed against a deliberately broken
      helper, and that was the real find.** No Qt makefile listed
      extern.h (or astrolog.h, outside two generated-table rules) as a
      dependency of anything -- so editing only a header rebuilt nothing,
      and the check ran the stale binary. Same failure shape as item
      31's astrolog.res, in the header dimension. All four makefiles now
      make every object depend on astrolog.h and extern.h (resource.h
      too for Windows), and the rerun failed the three assertions it
      should: break the bias, lose the round trip and the 0-Aries edge.
    - Every earlier header edit in this branch happened alongside .cpp
      edits, which is the only reason none of them shipped stale.
    - Also relearned: grep full build logs for `: error`, not ` error` --
      atlas.cpp legitimately prints "Zone rule error:" inside warning
      context, six times.

62. **Stages 2 and 3: one way to set a definition, one way to set a
    name.** The write half of the slot accessor and the display-name
    convention, together because their call sites interleave.
    - `ObjDefSet()` replaces the five separate array-store sites (`-Ye`
      and both dialogs in both builds), carrying the glyph rule inside:
      identity is type, body and **point** -- a north node is not the
      planet -- while calculation flags are not. Two deliberate changes:
      `-Ye` re-asserting a slot's existing definition keeps its glyph
      (the old code dropped it unconditionally), and a point-only change
      through a dialog now drops it (the dialogs compared only
      type/body). `-Ye` also gathers its whole definition, trailing
      flags included, into one OBJDEF before a single store, instead of
      storing four arrays at three separate moments.
    - `FObjIsCOBOf()` replaces the five open-coded "is the Vulcan slot
      redefined as planet N's center of body" predicates -- four wheels
      in xcharts1.cpp with the moon number as a literal, and RObjDiam()
      with it computed -- which the plan (Stage 2) flagged as the
      most-duplicated predicate in the core.
    - `SetObjDisp()` and `FObjDispCustom()` own the display-name
      convention: customised means the pointer differs from the
      `szObjName[]` constant. Nine guarded-clone sites and thirteen
      identity tests (ten of them in intrpret.cpp, which the plan
      missed) now go through them. One real fix rides along: restoring
      a slot's stock name used to *clone* it, so the slot read as
      renamed forever after and earned a spurious `-YD` line in every
      saved settings file; the setter repoints at the constant instead,
      and an assertion pins the round trip.
    - Single-field reads like `rgTypSwiss[..] == 4` stay direct on
      purpose: pulling a whole OBJDEF to test one field is noise, not
      clarity. The multi-field gathers (FSwissPlanet, both dialogs'
      pre-fills, SzObjSelDef) all go through ObjDefGet now.

63. **Stage 4: the per-object settings are one struct, and the flat
    arrays' own initializer proved the point on the way out.** The first
    storage change of the plan, taken on the maintainer's explicit and
    repeated decision that upstream merges are worth spending. `OBJSET
    rgobjset[oNorm1+1]` holds max orb, orb addition, influence, transit
    influence and color -- one named row a slot, each row commented with
    its object -- replacing rObjOrb/rObjAdd/rObjInf/rTransitInf/kObjU.
    The five rulership bonus weights that rode past the end of rObjInf[]
    are `rgrBonusInf[]` now, with `funObjInf` still answering the old
    indexes so scripts keep working.
    - **The flat initializer was broken in exactly the way the structure
      exists to prevent.** rObjOrb[]'s 85-slot initializer had 84
      anonymous values: one entry missing at Lilith, so she wore
      Fortune's 360-degree orb, every later slot shifted onto its
      neighbor's value, and the fixed-star row read zero. Found because
      the generator counted; located by machine-diffing against
      upstream's own astrolog.as, where a single insertion realigns
      every mismatch. A row-per-slot initializer with the object's name
      on each row cannot fail this way, which is the whole argument the
      maintainer had been making.
    - The struct rows were generated from the flat lists and verified
      statically against them (and astrolog.as) before the flats were
      deleted, orb column corrected. Three switch parsers that selected
      a target array by pointer across two index domains (-YA, -Yj,
      -Yk) were split by hand; everything else was mechanical, with the
      compiler flushing what the regexes missed.
    - **What the net caught during the change:** io.cpp's bonus writers
      spelled the index "oNorm1 + 1" with spaces and dodged the bonus
      regex, becoming an out-of-bounds struct read that one ASan run
      named; and the console build's Makefile had neither header
      dependencies nor a post-conversion rebuild, so its stale objects
      "found" phantom leftovers -- the plain Makefile now carries the
      same header-deps rule as the other four, placed *after* the link
      target, since a dependency line placed first becomes the default
      goal and quietly stops the build at the objects.
    - `tools/settings-round-trip.sh` is the end-to-end proof and now a
      standing check: one save/load/save through the console build
      reaches a byte-identical fixed point.
    - **The upstream-merge bridge is burned here**, deliberately:
      upstream addresses these five arrays ~130 times. The Windows build
      is unaffected and remains the oracle.

64. **The rulership cross-table invariant is asserted** -- the one piece
    of the declined "Stage 5" reference-data struct worth having, added
    2026-08-29. The `rulership` suite group pins, for each of the
    traditional, esoteric and hierarchical systems: the two "none"
    encodings (-1 sign-keyed, where 0 is the Earth; 0 object-keyed --
    the exact difference item 38's bug was made of), that every table
    entry indexes in range, and that every ruler or co-ruler a sign
    names lists that sign back in the object-keyed direction. Verified
    to fail on all three corruption classes before shipping.
    - **Only that direction is asserted, on purpose.** The reverse is
      legitimately looser: `ruler1[]` gives minor objects sign
      affinities `rules[]` never records (Ceres "rules" Virgo without
      being Virgo's ruler), and `rgObjEso2[oVul]` claims Virgo while
      `rgSignEso2[sVir]` stays empty -- upstream data, kept.
    - **The invariant is about the shipped defaults, not a runtime
      guarantee.** `-YJ` maintains the mirror through
      `AdjustRulership()`, but that function is deliberately lossy:
      a sign whose planet moves away keeps its stale primary ruler when
      there is no co-ruler to promote. astrolog.as's own `-YJ` block
      restates the defaults exactly, so loading it changes nothing and
      the group may run at any point in the suite.

65. **The defaults audit, and what its first run caught.**
    `tools/defaults_audit.py` (REFACTORING.md increment 1) machine-diffs
    data.cpp's compiled defaults against astrolog.as two ways: every
    initializer must hold exactly its declared count, and every value the
    .as restates must match, behind an allowlist of 27 verified upstream
    preferences (each checked against CruiserOne master before being
    excused). Six falsification classes confirmed caught before shipping.
    - **First run found `ruler2[]` one value short** -- 83 values in 84
      slots, the minors row missing an entry, upstream's bug too. Benign
      today only because everything past the gap is zero, so the fix
      (one added `0`) is behavior-identical; the next co-ruler added
      near the end would have landed one slot off.
    - **astrolog.as still carried the stale output of the -YR/-YRT
      writer bug.** The io.cpp writer was fixed earlier to emit `-YRT`
      for transit rows 52-133, but the shipped file was never
      regenerated, so its transit section restated the natal
      restrictions a second time. Corrected in place; behavior-neutral
      because ignore2[52..133] defaults are all restricted anyway.
    - With a file argument the audit runs its count leg against any .as
      (nrvate.as is clean).

66. **The settings round trip grew teeth, and five bugs came out.**
    `tools/settings-round-trip.sh` (REFACTORING.md increment 2) now has
    three legs: the maintainer's settings fixed point (as before); every
    boolean flag flipped at once, required to persist and to reach its
    own fixed point; and `tools/settings-fixture.as`, 31 value switches
    set to sentinels with per-line `; EXPECT <regex>` checks against the
    resulting save. Flag families that cannot flip are exempted with
    reasons in the script (the one-way `-0` lockdown family, `-v3`
    carrying its boolean in the value, bare `X` forced by the script).
    What the two new legs caught, all fixed and all shared-core:
    - **A buffer-overflow crash**: `SzLocation()`'s `static char
      szLoc[25]` needs 29 bytes when `=b1` (zodiac milliseconds) is on
      -- glibc fortify aborts the console build on any location print.
      Buffer sized to the worst format. Upstream's bug.
    - **A second overflow in `SzZodiac()`**: the AstroExpression degree
      hook's `"%15.15f"` writes 17-19 bytes into `szZod[16]` for every
      value, before its fixed-offset truncation runs. Buffer enlarged
      and the expression result bounded with `Mod()`.
    - **`-Yu` never reached a fixed point**: the writer packs
      `fEclipseAny` into the "0" suffix, but loading suffix-less
      `_Yu`/`=Yu` left the flag stale, so alternate saves wrote `_Yu`,
      `_Yu0`, `_Yu`... The parser now decodes both bits when the prefix
      is explicit; a bare toggling `-Yu` is unchanged.
    - **`:YXp0` multiplied by 2.54 per metric save/load cycle**: the
      switch parser read "21.59cm" with bare `RFromSz()` as 21.59
      inches, while both GUIs' dialogs already parse the same field
      with `RParseSz(pmLength)`. The switch now does too.
    - **`-YD` renames of standard objects were never saved**: the
      writer emitted `-YD` only inside the custom-slot and star loops,
      so a renamed planet or cusp lost its name on the next save. The
      object-customization section now writes them.

67. **A nested include no longer severs the switch-file payload
    channel.** Switches like -YY read in-band data from the file being
    parsed through `is.fileIn`, and all six file parsers cleared that
    global on exit instead of restoring it -- so a file included with
    -i from inside another file left the outer file's channel NULL.
    The live repro showed the blast radius was bigger than the survey
    predicted: the outer file's next payload switch errored "Switch
    only allowed in file context", which aborted not just the rest of
    that file but the rest of the *command line* after the -i. All six
    parsers now save and restore the previous channel.
    - Suite group `nested-include` pins it: an outer file with an
      include, then a one-city -YY payload, then one more switch, must
      load to the end with all three applied (and the test resets the
      atlas to lazy-load afterward, since its payload replaces the real
      city list). Falsified: reintroducing the clear fails 3 of its 4
      checks, the third reporting the inner file's value -- the outer
      aborted exactly where predicted.
    - Two suite-craft lessons paid for themselves: `FileOpen()` is
      read-only and *prompts interactively* for a missing file (the
      test's fixtures are written with plain fopen), and a test whose
      failure path raises PrintError must SetNoPopupQt(fTrue) or its
      failure mode is a hang on a message box, not a FAIL.
    - This is the interim fix; T3's parse context (REFACTORING.md)
      retires the global channel entirely.

68. **The switch registry exists, and fifteen switches live in it.**
    T3's M1 (REFACTORING.md): a compiled-in registry consulted by
    `FProcessSwitches()` by exact full spelling before its big switch,
    covering command line, settings files, and macros in every build.
    A ranged-setter descriptor table (`rgswranged[]` -- index domain,
    bounds, target slot and stride) absorbs -YAo/-YAa/-YAm/-YAd/-Yj/
    -YjT/-YjC/-YjA as *data*; handlers carry -Yj0/-Yj7/-YAD and the
    four rulership spellings. Cases 'A', 'j' and 'J' are deleted from
    `NProcessSwitchesRare()` (~140 lines of hand-rolled parsing).
    - **Proof of preservation**: a 26-invocation behavior matrix
      (valid, edge, names-as-indexes, and every error shape) run
      through the pre-M1 binary built from HEAD in a scratch worktree
      and the new one -- byte-identical output, plus the standing nets
      (suite, three round-trip legs whose fixture covers every migrated
      family, defaults audit which loads astrolog.as through the new
      driver, ASan, Wine scenarios).
    - **One deliberate strictness divergence from upstream**: the
      retired cases accepted garbage suffixes as accidental aliases
      (-YJq acted as -YJ7, -YAz as -YAa, bare -YA as -YAa). Only the
      spellings -HY documents exist in the registry; the rest now fail
      as any unknown switch would.
    - **The differential method found a crash of its own**: the pre-M1
      binary, living in a deep scratch directory, aborted at startup --
      `sprintf2`/`S()`/`SO()` were only bounded `#ifdef PC`, so every
      "bounded" formatted write in the program was plain sprintf on
      non-Windows builds, and FileOpen()'s path probing overflowed on
      a long executable path. The snprintf definitions are now
      unconditional, and FileOpen's two remaining unbounded probes use
      them. A deep install directory now degrades gracefully (a
      truncation warning) instead of aborting before main output.

69. **M2: the restriction, Ray and color families join the registry --
    39 switches migrated, six parser cases gone.** The ranged
    descriptor grew what the new families actually need: a value kind
    (real, boolean-into-byte, checked int, checked color -- each
    reproducing its retired case's own validation and error-parameter
    convention) and a post-store hook (`RedoRestrictions()` after
    -YR/-YRT). -YR/-YRT/-Y7O/-Y7C/-YkO/-YkA/-Yk0/-Yk7/-Yk became table
    rows; fifteen sub-switches (-YR0/1/2/p/Z/7/d/h/o/i/U/U0,
    -YkU/E/C) became handlers, quirks preserved (the -YR pairs parse
    their booleans through pmObject exactly as before). Cases 'R', '7'
    and 'k' are deleted from NProcessSwitchesRare().
    - Proven by the M1 differential method, extended: 61 invocations,
      1,193 lines of captured stderr and saved-settings output,
      byte-identical between the pre-M2 binary and this one. The
      harness now normalizes its temp paths and captures every
      migrated family's save lines -- and its first run caught its own
      append-after-rmdir bug, both binaries failing identically.
    - Same strictness note as item 68: accidental garbage-suffix
      aliases of these families (-YRx as -YR, -Ykx as -Yk) now error.

70. **M3: the parse context arrives, and is.fileIn is gone.**
    `FProcessSwitches()` takes a `PARSECTX *` (NULL from the command
    line, macros, and dialogs; the file parser passes one holding its
    own FILE* and name), so the -YY payload family reads the file being
    parsed through an argument on the stack instead of a global -- the
    channel whose clobbering item 67 patched is now structurally
    impossible, and the `is.fileIn` field, its six set/restore dances,
    and the B3 interim fix are all deleted. Registered this increment:
    -YD, -YS, -YU/-YUb/-YUb0/-YUx, -YF, -YE, the six -YI spellings over
    one phrase-table core, -YYt/-YYT, and -YY/-YY1/-YY2/-YY3 over one
    payload core. -Ye is the registry's first *prefix row*: its type,
    point, and flag letters ride in the switch spelling itself
    (-YemnHS...), so the handler receives the spelling and scans it
    exactly as the retired case scanned argv[0]. Eight more parser
    cases deleted; the registry holds ~60 switches.
    - Differential: 96 invocations, 1,891 captured lines,
      byte-identical -- including -zL/-zN, which pull the atlas and
      timezone payloads through the new context end to end.
    - Two strictness divergences of the established class, recorded:
      -YY with a garbage suffix acted as zone-links (-YYx, and -YYI,
      whose intended branch has sat dead behind a misspelled `#ifdef
      INTRPRET` upstream); -YI with an unknown suffix acted as -YIa.
      Both now error as unknown switches. Theoretical third: a -YY
      *payload* switch inside a macro string ran off the enclosing
      file's channel through the old global; macros now have no file
      context (text -YYt/-YYT are unaffected).

71. **M4: NProcessSwitchesRare() is deleted.** The rare parser's last
    ~30 cases moved to the registry and the function, its case in
    FProcessSwitches(), and its extern are gone. A fourth row shape
    arrived for the ~20 pure toggles -- `{"Yd", &us.fEuroDate}` flag
    rows, with the =/_/-/: prefix semantics applied by the dispatch --
    plus a dozen small handlers carrying their quirks intact (`_Yz0`
    restoring automatic Delta-T with no argument, -Y1/-Y10's `!fAnd`,
    -Ys's optional peeked offset, the -Yu fixed-point semantics), four
    more prefix rows for the digit-suffix families (-Ya, -Yq, -Yi,
    -Y5), and a -YX prefix bridge that forwards to the graphics rare
    parser until that family migrates. Differential: 156 invocations,
    3,236 captured lines, byte-identical against the pre-M4 binary.
    - **The suite failure that verification hit was a pre-existing
      flake, not M4** -- proven by the same minimal group combination
      (menu-actions, chart-list, midpoint-glyph) failing on the pre-M4
      build too. TestChartListFilterQt() leaves ciCore on a synthetic
      chart whose date is fixed but whose *time* is "now", so the
      midpoint-glyph render drifted with the clock, and at the double
      scale menu-actions leaves behind, the forced slot's label sat on
      or off the canvas depending on the minute. The test now pins
      ciCore (to ciTwin's fixed data) beside its existing us/gs pin --
      item 4's pin-the-time lesson, applied where it was missed.

72. **M5: NProcessSwitchesRareX() is deleted -- the second of the four
    parsers gone.** The whole -YX graphics family moved into the
    registry (all inside `#ifdef GRAPH`, with SWISS/PS/CONSTEL guards
    riding on their rows and handlers exactly as the retired cases
    carried them): exact rows for the two-dozen plain spellings, prefix
    rows for -YXG and -YXf whose sub-letter rides in the spelling, a
    flag row for -YXe, and shared cores for the -YXD and -YXA variant
    pairs. The M4 bridge, the function, its banner, and its extern are
    gone. Differential: 192 invocations, 4,883 captured lines,
    byte-identical against the pre-M5 binary.
    - One deliberate divergence that is a fix, not a transliteration:
      the retired -YXW case read argv[1] with no arity check at all --
      undefined behavior on a bare "-YXW", not preservable behavior --
      so the registry handler checks like every other switch.

73. **M6: NProcessSwitchesX() is deleted -- three of the four parsers
    gone.** The whole -X graphics family is registry-resident. The row
    tables gained a `grf` bits field, and its first bit does what the
    retired main-parser case 'X' did around its entire sub-parser:
    every row marked `grfSwGraphics` is refused when -0X has locked
    graphics away, and turns graphics mode on when it succeeds --
    family-wide behavior declared once on the rows instead of coded
    around a call site. Seventeen -X toggles became flag rows, the
    chart modes (-XX/-XW/-XG/-XP/-XZ) share their optional
    rotation/tilt shape across suffix spellings mapped to one handler
    each, and -Xb/-XM/-XE/-XU/-XL are prefix rows. The main parser's
    case 'X' and the 480-line function are deleted.
    - Differential: 259 invocations, 6,791 captured lines,
      byte-identical -- after it caught a real mistake in review: -XE
      was first registered as an exact row and "-XE1" fell through to
      "Unknown switch". The harness exists precisely for that.
    - The -0X lockdown corner narrowed: a garbage -X spelling under
      -0X used to say graphics were not allowed and now says unknown
      switch, since the registry checks the lockdown only for
      spellings that exist. Same strictness class as items 68-72.

74. **M7: the restriction and aspect cluster leaves the main parser.**
    Cases 'R', 'C', 'u', 'U' and 'A' are deleted from
    FProcessSwitches(). -R is a prefix row whose handler carries the
    whole family -- category presets by suffix, the -RT second-level
    prefix, and the variadic object list, counting its consumption by
    argc delta. Quirks preserved exactly, including the stale second
    letter ("-RTu0" reads ch2 from where "-Ru0" keeps it, so it acts
    as "-RTu", as it always has). -C and the -u categories share one
    toggle-and-sync core; -U covers the star sorts and exo-transit
    spellings; -A covers its five shapes (count, three flags, and the
    four single-slot orb/angle setters). Differential: 314
    invocations, 8,361 captured lines, byte-identical.
    - Strictness divergences of the established class: suffix garbage
      that used to alias a defined spelling ("-C5" as -C, "-uz" as -u,
      "-Az"... actually -A<unknown> remains the aspect-count branch,
      transliterated) now errors where the registry has no row.

75. **M8: the chart-computation letters leave the main parser.**
    Fifteen cases deleted -- the -b ephemeris-selection soup (digit
    suffixes stand alone, every other spelling also enables ephemeris
    files, exactly as the case fell through), -c house systems with a
    new `nSwitchStop` dispatch sentinel reproducing the WIN
    screensaver's stop-parsing-and-succeed quirk, the -s zodiac and
    degree-form family with its optional peeked offset, -h centering,
    the whole -p progression family (six sub-shapes), -x harmonics,
    -1/-10/-2/-20 solar charts with their fAnd quirks over one core,
    -4 dwad, -F/-Fm forced positions, and flag rows for
    -3/-9/-f/-G/-J. Differential: 377 invocations, 10,175 captured
    lines, byte-identical.
    - The one candidate diff the matrix surfaced was its own stale
      line: a -Yj run passing one value too many, whose leftover "43"
      token used to error as "Unknown subswitch '-43'" via case '4'
      and now errors as an unknown switch -- the established
      strictness class, this time only rewording an error that was
      always an error. The FErrorSubswitch message wording for retired
      digit cases goes with them.

76. **M9: the chart-type letters leave the main parser.** Twenty-four
    cases deleted -- every text chart selector (-v/-w/-g/-a/-m/-Z/-S/
    -l/-j/-7/-L/-K/-d/-D/-E/-8/-e/-t/-T/-B/-V/-P/-N/-I). Nearly all
    are prefix rows, which for these are *exactly* equivalent to the
    retired cases for every token (a prefix row and a case letter see
    the same spellings); the handlers that used to walk argv[0] with
    ich -- the transit and progression suffix machinery of -t/-T/-B/
    -V/-dp -- walk szSwitch with a local cursor. -S/-D/-7 became flag
    rows and -e (the everything-toggle) an exact row. The X11
    -geometry and -display long spellings ride the -g and -d prefix
    rows just as they rode the cases.
    - One transliteration bug caught by re-derivation before the
      differential ran: -dp's cursor got a spurious initial increment.
      The 460-invocation matrix (12,582 captured lines, byte-identical)
      then covered every cursor-walking spelling.

77. **M10: the main parser's switch statement is deleted.**
    FProcessSwitches() is 44 lines -- the prefix prologue, the registry
    consult, and an unknown-switch error. It measured 1,774 lines when
    REFACTORING.md's survey ran. The last twenty-two cases moved: help,
    macros (-M runs and defines through the registry now, so macro
    strings recurse through the same dispatch), chart-info entry (-n/
    -q/-z with a shared slot-store helper), -i with its success-and-
    stop lockdown semantics preserved via nSwitchStop, -o's trailing
    comment-list scan, day arithmetic (bare "-", "=", "+", and the
    "--t" spellings, with the empty-name row distinguishing a bare
    prefix char from an empty token by pointer position), relationship
    charts, the chart list, -k colors, the per-build -W dispatch, the
    -0 lockdown walk, -;/-@/-. (the -@ row is a prefix row because .as
    file headers like "@AD800" parse as switches), and the entire ~
    AstroExpression hook table, its sixty-odd spellings transliterated
    verbatim.
    - Final differential: 529 invocations, 14,378 captured lines,
      byte-identical. The migration ran M1-M10 in one day: about 250
      spellings, four parsers dissolved (NProcessSwitchesRare, RareX,
      X, and the main switch), every step proven against the previous
      binary.

78. **The registry is self-checking, and -YYI works for the first time
    in its existence.** Two nets for the new architecture: suite group
    `registry` enumerates all 260 rows through a new
    FSwitchRegistryRow() accessor and pins the structural invariants
    (unique spellings; no prefix row shadowing a row scanned after it
    -- the -XE mistake class, now structural; exactly one empty
    spelling), falsified with a planted duplicate and a hoisted prefix
    row. And `tools/registry_audit.py` extracts every spelling the -H
    help text documents and FOutputSettings() writes -- 449 of them --
    and resolves each against the registry with the dispatch's own
    exact-then-prefix rule.
    - The audit's first run found -HY documenting `-YYI <text>`, a
      switch that had never worked: its implementation sat behind
      upstream's misspelled `#ifdef INTRPRET`, so the spelling fell
      through to the atlas payload branch (any file using it would
      have eaten the rest of the file as zone links). The registry row
      now does what the help always said, under the correctly spelled
      guard, and the audit holds the manual and the program to each
      other from here on.

79. **D2: the influence stanzas ride the rulership family table.**
    ComputeInfluence() and ChartInfluence()'s sign section walk a
    three-row RULERSYS table (system gate, sign-keyed pair, object-
    keyed pair -- the same pairs item 64's suite group cross-checks)
    instead of nine hand-copied stanzas: the clone-and-swap shape that
    bred items 37 and 38. The two deliberate non-uniformities survive
    with their reasons attached: the house-cusp loop's traditional
    line counts only the primary ruler, unguarded, as it always has;
    and the object-keyed direction's "if nonzero" secondary test
    stays distinct from the sign-keyed "> 0". Proof: influence and
    esoteric charts for a fixed date under six rulership-restriction
    states, 3,426 output lines, byte-identical against the pre-D2
    binary.
    - The battery caught the midpoint-glyph flake AGAIN, and this time
      the root: item 71's pin was "ciCore = ciTwin", but ciTwin is
      mutated by the relationship groups to charts of "now" -- the pin
      only held while the clock cooperated, and failed deterministically
      by evening. The test now pins ciCore to literal constants. Two
      clean full-suite runs at the failing clock time.

80. **The migration day's harnesses are committed, and the
    continuation state is written down.** `tools/switch-matrix.sh`
    (the 529-invocation behavior matrix that proved M1-M10) and
    `tools/influence-matrix.sh` (D2's computation matrix) moved from
    session scratch into the repo, each headed with the worktree
    baseline method. REFACTORING.md gained "The registry as built" --
    the architecture map, enforced invariants, verification method,
    strictness policy, harvest constraints, and the specified next
    increments (C3, the D1 pair merge, F1, E1) -- written so a session
    with no memory of the migration can continue it correctly.

81. **C3: ComputeEphem()'s skip predicate is now FSkipEphem().** The
    six OR'd clauses deciding "compute this object or not" (calc.cpp)
    became five named sequential ifs, each comment stating its
    verified reason -- the JPL-Earth clause's first-draft comment
    guessed wrong and was corrected against rgObjJPL[] (Earth has no
    entry; it is the query center). Proven with an 11-case -v position
    differential old-vs-new (restriction, node, heliocentric, Placalc,
    -ba, Moshier, restricted-midpoint cases), byte-identical; suite
    3036/0; Windows cross-build clean. Process note: the first
    differential attempt ran the console binary without env -u DISPLAY
    and _X, popping chart windows on the real desktop -- nrvate.as
    turns graphics on. The ad-hoc-differential discipline is now in
    REFACTORING.md's verification recipe.

82. **D1 first pair: the aspect lists share one core.** ChartAspect()
    and ChartAspectRelation() (~70% identical clones, the pattern that
    produced item 38) are now wrappers over a static
    ChartAspectCore(flag fRel) in charts1.cpp; the charts2.cpp copy is
    deleted. fRel picks the grid orientation, position source, sort
    keys, AstroExpression letters, interpret routine, and line tail;
    the search scaffold, ordering logic, and summary exist once.
    Proven byte-identical over a 32-case differential -- both lists x
    nine sort keys x parallel/distance/applying/exact, summary,
    interpret. The differential's first version was green over nothing
    twice: -a on the command line silently toggled the aspect list OFF
    (nrvate.as line 281 already sets -ao; the = force-on prefix is
    required), and grep without -a printed nothing on chart output
    (binary detection). Both are in REFACTORING.md's recipe now, with
    the rule: prove the cases differ from each other before trusting
    a green diff.

83. **D1 second pair: the midpoint lists share one core.**
    ChartMidpoint() and ChartMidpointRelation() are wrappers over
    static ChartMidpointCore(flag fRel) in charts1.cpp; the charts2
    copy is deleted. The pair differed only in iteration domain, grid
    cell, position source, 'm'/'M', one space, and the
    PrintAspectsToPoint arguments; fRel carries all six. Proven over a
    16-case differential (list, summary, midpoint aspects, interpret,
    3D house, -RO required-object, parallel), byte-identical, suite
    3036/0, Windows cross-build clean. Coverage notes: nrvate.as
    already sets -ma and the summary, so those cases equal =m by
    construction; fParallel needed "=ap _a" to force (it belongs to
    the -g/-a families; -Yp is something else entirely).

84. **D1 survey closed: the other "clone pairs" mostly aren't.** Read
    closely before merging, the Listing and Grid twins are different
    layouts (multi-chart columns + delta vs. houses and rulerships;
    axis-labeled matrix vs. diagonal square), and the Interpret trio
    differ in actual prose -- InterpretAspectRelation names person 2
    where InterpretAspect says "their", has a conjunction-only
    fallback sentence, and InterpretMidpointRelation drops the
    life-area tail. Merging any of these trades duplication for
    conditional-text soup, so they stay as they are, on purpose.
    ChartAstroGraph/ChartAstroGraphRelation is the one true clone
    left (~400 lines each, every array doubled to [2][objMax]) --
    a dedicated-session merge with a -L differential if ever.
    Verdicts recorded in REFACTORING.md's D1 finding.

85. **F1: the projection chains exist once.** The eighteen
    (Loc|Equ|Ecl|Pri|Ear)To{Horizon,HorizonSky,Telescope} helpers in
    xcharts1.cpp were three near-identical chain sets differing in
    their terminal plot transform plus two real deltas (the sky
    chart's Loc skips the azimuth flip because PlotHorizonSky orients
    azimuth itself; the telescope's Ear mirrors latitude). The chain
    logic now lives once in static *ToProj() functions over a PROJ
    context; all eighteen names remain as three-line adapters, so no
    call site or extern.h signature changed. Proven byte-identical
    over a 12-bitmap -Xb differential: -Z, -Z0, -XZ each rendered at
    default, =YXe (ecliptic), =Yf (refraction), and both -- with a
    distinctness pass showing every variant changed the bitmap, so
    both branches of every chain ran. Suite 3036/0, Windows
    cross-build clean. The -Xb headless bitmap render is a new net:
    pinned-date graphics differentials without a display, window
    manager, or the Qt harness.

86. **E1 closed: the primitives are already normalized.** A shape
    audit of all 28 xgeneral.cpp drawing primitives (tag-mapping each
    function's #ifdef/format-branch order) found the
    one-block-per-target shape E1 asked for already holds everywhere
    it can: the QT porting pass touched every primitive and left them
    normalized. The apparent exceptions are deliberate -- DrawFill
    has no X11 screen branch because X11 offers no flood fill (and
    PS/SVG/wireframe are commented not-implemented); DrawEllipse2
    just orders its QT block before X11; DrawSz is dense because a
    per-character loop must dispatch across seven text targets, and
    restructuring it risks rendering for no behavioral gain. Verdict
    recorded in REFACTORING.md; no code change, on purpose.

87. **Two closes and a small split.** (a) The T3 harvest idea of
    deriving FOutputSettings() flag values from registry rows was
    measured and closed: of 42 simple flag emissions only 9 spellings
    are exact registry flag rows; the rest are suffix-parsed inside
    prefix handlers, out of reach of any row lookup. The audits and
    round-trip sentinels already cover the drift classes. (b) F3:
    DrawPrint()'s three calling conventions became three functions --
    DrawPrintTo(x, y) and DrawPrintShift(dx) own the cursor,
    DrawPrint() only draws. The "many call sites" was two; every
    other caller already used the text convention. Proven with a
    4-bitmap sidebar differential (wheel, relationship wheel,
    exoplanet chart, sidebar off), byte-identical; suite 3036/0;
    Windows cross-build clean.

88. **The generated-help idea is closed, and with it all of T3.** The
    -H text is pedagogical prose with many-to-one structure (one line
    documents nine sort-key rows; section headers interleave; ifdef'd
    composites are sprintf-built), so a generated version would
    degenerate into an ordered (guard, string) list -- which is what
    the PrintS sequence already is -- while registry_audit.py already
    parses those lines and cross-checks every documented spelling
    against the registry. No drift protection would be added. T3 now
    stands fully closed: M1-M10 migration, registry hardening, and
    both harvest ideas measured and closed by the same standard.
    Remaining plan items are the documentation themes (T1, T8, A4,
    C4) and the optional AstroGraph clone merge.

89. **C4: the backend selector's state space is written down.** The
    five fields that jointly choose the ephemeris backend now carry a
    state table at their declarations (astrolog.h): the six reachable
    backends, the inert corners (fPlacalcPla wins over nSwissEph),
    and the trap that every -b backend suffix falls through to also
    toggle fEphemFiles -- benign in practice because the settings
    writer emits forced prefixes and the dialogs assign fields
    directly. Found while writing it: the derived reading layer C4
    wanted already exists as the FCm*() macros in extern.h, and the
    dialogs already collapse the choice to a six-value list, so the
    "collapse to an enum" step is deferred indefinitely as
    relocation, not creation.

90. **A4: the lifecycle contract is written, and the probe corrected
    a doc.** REFACTORING.md now records what runs once
    (InitProgram/FinalizeProgram), what is re-entrant (CastChart,
    with the relationship-mode fix as the known exception), the
    first-use caches, and the ordering constraints. Writing it
    surfaced a mechanism error in QT_TESTING.md: the claim was that
    SwissEnsurePath() latches the path so a late -Yi does nothing --
    in fact -Yi has cleared the latch since upstream 7.00 and the
    path re-applies; the probe showed Cupido still reads 0 after a
    late -Yi1 because the Swiss library caches its orbital-elements
    state internally on the first failed load. The rule stands, the
    reason is now the true one, and the probe (0.2s) settled in one
    run what code reading got wrong.

91. **T8: CONVENTIONS.md exists.** The conventions that lived in
    folklore and incident reports are one verified document at the
    repo root: the Hungarian dialect, the two spellings of "none"
    (object-keyed 0 vs sign-keyed -1, item 38's root cause), the
    SwitchF/ChDashF prefix machinery, the chart-info alias macros,
    the bare feature-word collision rule (Qt headers before
    astrolog.h; new macros take prefixes), buffer bounds and
    sprintf2, the PrintS/FieldWord static-state output machinery,
    handler return codes, the goto LDone idiom, the four event loops
    and the full checklist for adding a command, the
    label-is-an-identifier invariant, the object taxonomy predicates,
    and pointers to the registry and lifecycle documents. CLAUDE.md's
    orientation list now includes it.

92. **T1 move (1): the state classification is on the structs.** The
    four global structs in astrolog.h now carry their contracts: US/GS
    are user intent (serialized where persistent, mutations owned by
    switches and dialogs), IS/GI are derived and scratch (never
    serialized), shadowed names like fProgress mark the boundary --
    us.fProgress is the request, is.fProgress is what the current
    cast actually did -- and computation may only *borrow* a settings
    field via the save/restore idiom, every borrow restoring. Writing
    it sharpened the T1 finding: the interleaving problem was never
    misfiled fields, it is the ~60-site *Sav dance, which is exactly
    what T1's move (2) (a push/pop home for borrows) addresses next.

93. **The forced-slot name rule needed the rename in its trigger.**
    Reported by the maintainer the first evening the rule shipped:
    the Part of Fortune drew as "For" instead of its glyph. Their own
    config does "-Fm 19 1 2  ; POF = Sun/Moon midpoint" -- a forced
    slot that KEEPS its name is that body computed by another
    formula, not a different body wearing a stale glyph, so the
    draw-the-name rule now requires forced AND renamed. The condition
    is a predicate (FDrawObjectAsName) so the suite pins all three
    corners; the test was falsified against the reverted condition
    (and the first falsification run was void -- a const error left a
    stale binary passing, caught by the build-gating habit). The same
    evening's second report, house systems resizing signs, measured
    as NOT a regression: pre-last-night and current binaries produce
    pixel-identical change profiles across Plac/Koch/Equal/Whole (the
    lone differing pixels are Fortune's glyph); the described
    behavior is the House Wheel (-w) working as designed.

94. **T1 move (2): borrows have one home.** The Borrow scope guard
    (astrolog.h, C++ only, all builds are C++) saves at construction
    and restores at the closing brace on every exit; CONVENTIONS.md
    carries the rule. Exemplars: the settings writer's two scoped
    borrows (byte-identical written file, round trip green) and the
    print path (gotcha 3's site), where two hand-written five-field
    restore blocks became one closing brace and the out-of-memory
    warning now fires after the restore, so the repaint behind the
    modal cannot happen at print scale. The console Makefile links
    with g++ now -- the guard's destructor is the first thing in the
    core needing the C++ EH runtime. Conversion of the other ~60 *Sav
    sites is opportunistic by policy.

95. **Phase 2 planned: polish the registry's rough edges.** The
    registry retrospective (asked for by the maintainer) conceded
    four things honestly -- not less code, suffix soup bottled rather
    than drained, a 5,294-line astrolog.cpp god-file, and a
    7-parameter handler signature -- and the maintainer asked for a
    plan to do the hard cleanup. REFACTORING.md gained "Phase 2 --
    the polish plan": tranche 1 finishes what the registry started
    (P1 extract switch.cpp, P2 pack the handler arguments, P3
    declared arity closes A2, P4 de-soup by measure), tranche 2 is
    the deferred heavy items (P5 AstroGraph merge, P6 the Borrow
    campaign, P7 prefix the colliding feature macros), tranche 3 is
    survey-gated (P8-P10). Same rules as phase 1: nets on every
    increment, measured closes allowed, docs in the same commit.

96. **P1: the command surface has its own file.** switch.cpp (4,453
    lines) now holds the registry tables, ~99 handlers, the dispatch,
    and FProcessSwitches(); astrolog.cpp is back to being the 896-line
    program shell. The cut was verified clean beforehand (no static
    references cross the boundary in either direction) and surfaced
    exactly one hidden dependency: AdjustRulership() was non-static
    but never declared in extern.h. All five makefiles gained the
    object. The switch-matrix gate came back byte-identical over
    14,378 lines -- after the first run violated the harness's own
    documented rule (both binaries at equally short paths; the deep
    scratchpad baseline emitted the Swiss path-truncation warning on
    every invocation, exactly as the header warns).

97. **P2: handlers take one argument pack.** PARSEIN (astrolog.h)
    carries argc/argv, the decoded =/_/- prefix flags, and the parse
    context; FProcessSwitches() builds it once per switch, and all
    187 handler signatures plus the wdriver/qtdriver -W passthroughs
    collapsed to (szSwitch, PARSEIN *pin) / (pos, PARSEIN *pin). The
    interesting discovery: the 7-parameter threading was never
    laziness -- the FSwitchF()/FSwitchF2() macros scope-capture
    fOr/fAnd/fNot, so every function touching a flag needed the
    names in scope. The macros now read through pin, making the
    contract explicit ("any function using them takes a PARSEIN
    *pin", stated at the typedef). NSwR keeps local walking copies of
    argc/argv on purpose (it consumes arguments in a loop and returns
    argcIn - argc; writing through pin would corrupt the caller).
    Two helper functions (NSwCategory, NSwoCore) joined the
    convention when the compiler flagged their macro use. Matrix
    byte-identical over 14,378 lines; full battery green including
    win-tests, since wdriver.cpp changed.

98. **P3: rows declare their arity.** SWITCHDEF gained carg (trailing
    field; unconverted rows stay valid unchanged) and the dispatch
    makes the same FErrorArgc call the handler used to, with the
    row's spelling as the label. 41 handlers qualified under the
    strict rule -- the check is literally the first statement, the
    error label equals the row spelling, and the handler serves
    exactly one exact row. The skip list is as informative as the
    conversions: sixteen rows error under a different label than
    their spelling (Yj0 errors as "Yj" -- upstream's message
    choices), and prefix rows have per-suffix arity, so all of those
    keep hand-rolled checks with the reason stated at the carg field.
    Nets: a 41-spelling missing-argument stderr byte-diff and the
    529-invocation matrix, both identical; suite 3039/0; win build
    clean.

99. **P4 first cut: the AstroExpression hooks are a table.** The
    biggest soup pot -- NSwTilde's 87-comparison if-chain mapping ~54
    spellings to us.szExp* slots -- is now rgswtilde[], a fourth
    registry table of exact (spelling, slot) rows scanned between the
    ranged and handler tables; the residual handler keeps only the
    imperative forms (~0 ~M ~1 ~2 ~3, bare ~) and the unknown-suffix
    error. Former garbage-suffix aliases (-~gz acted as -~g) now
    error, per the standing strictness policy. Transcription was
    proven mechanically: a script simulated the OLD if-chain from git
    for each spelling and compared the landing slot against the new
    table -- all 54 match, including the three-level ~v/~v3/~v30
    ternary. Nets: 108-case store/arity differential byte-identical;
    matrix identical; both audits clean. Two process catches: the
    settings writer never persists expression hooks, so the first
    store-leg proved nothing until replaced by the chain simulation;
    and registry_audit.py had been reading astrolog.cpp since P1
    moved the tables -- the phase-2 batteries had dropped the audits,
    which is how it stayed unnoticed. The audits are back in the
    battery, and the audit reads switch.cpp's four tables now.

100. **P4 closed: de-souped by measure, verdicts written down.** The
    second cut promoted the two-flag chart families -- -l/-l0, -j/-j0,
    -K/-Ky, -Q/-Q0, -8/-80 -- onto flag rows via a new optional pf2
    field (the "suffix also enables the base chart" shape), and -v0
    onto a plain flag row; five handlers deleted. Proven by a 39-case
    output differential (13 spellings x 3 prefixes, byte-identical,
    cross-family distinctness verified), the matrix, both audits, and
    the suite. Everything still a prefix handler now has its reason
    in a verdict comment at the handler table: -m's suffixes combine
    (-ma0 means summary+aspects+midpoints -- a meaningful spelling no
    row can carry), -b shares the fEphemFiles fall-through, the chart
    info parsers choose argument shapes by suffix, and the list
    walkers take variable arguments. Registry: 318 rows; prefix
    handler rows 61 -> 56 across P4 (the count includes non-def
    prefix uses; grfSwPrefix mentions total 43 in the def table).
    Suite 3039/0. Tranche 1 of phase 2 is complete.

101. **P5: the astrocartography twins share one core.** ChartAstroGraph()
    and ChartAstroGraphRelation() -- the one true structural clone left
    from D1's survey (~385 vs. ~430 lines, every array doubled to
    [2][objMax]) -- are now wrappers over static
    ChartAstroGraphCore(flag fRel) in charts1.cpp; the charts2.cpp copy
    is deleted. The core was *derived, never retyped*: the relation
    function's exact text, extracted with CRLF intact, transformed by
    seventeen counted exact-string replacements (chart count, position
    sources cp0/planet[] vs. rgpcp[1..2], line prefixes via an empty
    sz2[0], ring indexes i3/i4 through the crossing search, and the two
    pairing conditions single mode adds: k != l against MC/IC, ordered
    pairs minus the Node axis against Asc/Des), then the diff reviewed
    hunk by hunk against the plan before splicing. One asymmetry kept
    on purpose, commented at the site: -c3 3D houses zero the altitude
    in single charts only, as the twins always did. The gate the D1
    verdict specified: a 27-case -L differential
    (single/synastry/dual/transit/progress x seconds off/on/1K x step
    x 3D x -R/-RT restrictions x -YRZ angle restrictions x interpret x
    -~L crossing filter x crossings off), every case proven pairwise
    distinct and the battery proven deterministic *before* being
    trusted -- the first run was green over nothing twice (the qt
    binary never prints console charts, use ./astrolog; =b0 force-on
    was a no-op because nrvate.as already has seconds on, _b0 is the
    off case). Byte-identical old vs. new on the first post-merge run.
    Suite 3039/0, six audits, round trip, ASan suite, win-tests.

102. **P6 opens: the gi.nScale family borrows (5 sites).** The Borrow
    campaign's first family: every `nSav = gi.nScale; gi.nScale =
    gi.nScaleTextT; ... gi.nScale = nSav;` dance in xcharts1.cpp --
    two in XChartOrbit (sign boundaries, aspect lines), three in
    XChartSphere (sign labels, house labels, aspect lines) -- is now
    a braced `Borrow bScale(gi.nScale, gi.nScaleTextT)`, scoped to
    restore at exactly the statement the hand-written restore sat at
    (the sphere aspect site needed its own brace: DrawObjects reads
    gi.nScale right after the old restore). Both nSav locals deleted.
    Gate: 8-case -Xb bitmap differential (orbit/sphere x base, -Xs
    300 -XS 100 scale contrast so a broken restore is visible, =Xe
    =XA aspect lines, =XC and =J block-skipped variants), pairwise
    distinct, deterministic, byte-identical after conversion. Suite
    3039/0, audits clean.

103. **P6 family 2: the tight scopes borrow (4 sites).**
    XChartTelescope's fEclipseAny wrap around one NCheckEclipse call
    and its fSeconds+nDegForm pair around the axis-label formatting;
    XChartSphere's two cp0 dances (install *pcp for FCreateGrid, and
    for the EnumMoonsRing init call). The FCreateGrid site's old code
    returned with cp0 still holding *pcp on the allocation-failure
    path -- the guard restores on that exit too, the exact leak class
    Borrow exists for (unreachable in the differential; noted, not
    proven). Three unused Sav locals deleted. The gate grew teeth
    this family: sabotage runs proved the eclipse case (a real
    solar-eclipse date, 1999-08-11 London), and exposed that both
    sphere cp0 sites are self-assignments in a single chart (pcp ==
    &cp0) -- only a relation sphere exercises them, and moons need
    =X8 _R8 -- so the battery gained sphere-moons-rel, and every one
    of the four sites now has a case a sabotaged conversion visibly
    breaks (value sabotage for the borrow values; a forced
    wrong-restore for the degform pair, caught by the =YXe case's
    next loop iteration). 14 cases, pairwise distinct, deterministic,
    byte-identical old vs. new. Suite 3039/0, audits clean.

104. **P6 closed: the campaign converts what is scope-shaped and
    writes verdicts for what is not.** Family 3: XChartTelescope's
    whole-span fRefract override is a braced function-level Borrow
    (no early exits in the 205-line span; `git diff -w` shows the
    edit is five lines, matching the campaign's no-reindent brace
    style), and XChartMoons' aspect-label nScale dance -- hidden from
    the Sav grep behind the name `k`, its span containing a
    FCreateGrid early return that leaked the scale -- borrows too,
    with a new moons-labels case (=8 =XL =XA _R8) sabotage-proven to
    see it. The fRefract case (=Yf, distinct output) cannot see a
    value sabotage inside its span on any case tried -- the span's
    projection path never visibly consults it here -- recorded as
    proven-by-construction (one-line value, brace-only diff) rather
    than by net. Campaign ledger for xcharts1.cpp, 11 sites
    converted, the rest stay with reasons: XChartSphere's fRefract
    restores *inside* the per-ring loop while saving outside it (an
    ordering no scope guard can express); the three ignoreSav arrays
    don't assign (and XChartSector restores at two points, XChartMoons
    pairs its restore with AdjustRestrictions and a recast -- compound
    cleanup, not field borrowing); DrawCalendar's Mon/MonT/cp shuffle
    is chart-swapping, not borrowing; XChartMoons' objCenterSav is a
    data source read long after its restore. One observed defect
    recorded for a deliberate fix: XChartMoons' FCreateGrid
    early return skips the entire cleanup block (ignore restore,
    fMoonMove restore, the recast) -- upstream shape, needs a real
    fix, not a conversion (fixed in item 106). 16-case -Xb battery, pairwise distinct,
    byte-identical throughout. Suite 3039/0, audits clean.

105. **P7: the colliding feature macros carry real names.** `PS` is
    `PSCRIPT`, `META` is `METAFILE`, `TIME` is `TIMEFUNC` -- 104
    preprocessor-line renames across 13 files, swept by a script that
    touched only lines starting with `#`, leaving the string "PS" in
    the atlas country table and every prose comment alone (a residue
    grep proves it). The proof a rename like this deserves: object
    code cannot change, and it didn't -- console, Qt, and test
    binaries byte-identical, and all 33 Windows objects too (the
    .exe embeds link timestamps, so objects are the comparable
    artifact; also learned: `make -f Makefile.win` objects are
    deterministic, the exe never is). The include-order rule -- Qt
    headers before astrolog.h, gotcha of this port since day one --
    is deleted from CONVENTIONS.md, and the deletion is proven both
    ways: a scratch TU including astrolog.h first compiles against
    the new header and dies on Qt::META against the old one. The
    three include-site comments (astrolog.h's Qt block, qtdriver,
    qtdialog) now say the order is a convenience, not a requirement.
    Suite 3039/0, audits clean. Names checked against mingw and Qt5
    headers for collisions before choosing.

106. **The XChartMoons cleanup leak is fixed, and proven live.** The
    early return item 104 recorded (bottom-charts FCreateGrid failure)
    now lands on an LDone: label before the cleanup block -- the
    goto-to-cleanup idiom xcharts2.cpp already uses -- so the moons
    chart's restriction overrides, us.fMoonMove, and the recast happen
    on every exit. Shared-core fix, no QT guard; both builds get it.
    Proven the way the working method demands: a scratch probe (state
    snapshot, SetChartModeQt(gMoons) with FCreateGrid forced to fail
    by a scratch env hook, compare) prints CLEAN with the fix and LEAK
    without it -- fMoonMove flipped and 27 ignore[] entries corrupted
    into the session. Both scratch edits reverted before commit.
    Valid paths proven untouched by the 16-case -Xb battery against
    the HEAD baseline, byte-identical. Suite 3039/0, audits clean.
    (The docs for this item shipped one commit late: the doc script's
    assertion failed and the commit ran anyway -- rule 4's exact
    half-commit shape, repeated because the statements were sequential
    instead of chained.)

107. **P10 opens tranche 3: the custom-object Swiss mapping exists
    once.** The C5 survey found its cache half already closed (A4's
    lifecycle contract, item 90) and measured its mapping half: five
    copies of the standard-object-to-SE-index motif in calc.cpp. Two
    were line-identical eight-way chains -- the nested type-2 custom
    maps in FSwissPlanet()'s direct and central-object translations,
    the same clone shape that bred item 38 -- and are now one static
    FSwissFromObj(); the central path also reads its custom
    definition through ObjDefGet() instead of the raw rgObjSwiss/
    rgTypSwiss arrays (same store, one accessor). The three remaining
    prefix copies (FSwissPlanet direct, central, FSwissPlanetData)
    share only a four-branch prefix and differ in coverage on purpose
    -- collapsing them would change which objects map at which sites
    -- so they stay, with the measured reason recorded in the C5
    finding. Nets: a 14-case -v positions differential (one case per
    cloned-map branch, geocentric and central via -h with slots
    referenced by number because nrvate.as renames the display names,
    plus asteroid/fictitious/moon/COB/part central types), pairwise
    distinct, deterministic, byte-identical; and the item-99
    old-chain simulation over the whole object domain, zero
    mismatches. Suite 3039/0, audits clean.

108. **P10/G1: the atlas lookups deliver rows through one sink.** The
    size_t that was an HWND on Windows and a boolean on Qt is a flag
    now, and all three atlas display functions hand result rows to
    the pfnAtlasRow sink (the Qt port's own mechanism, renamed from
    pfnAtlasRowQt) -- wdialog.cpp implements the Windows end over a
    port-owned handle, and atlas.cpp's #ifdef WIN clusters are gone.
    The survey first found a real port bug: the TimezoneChanges sink
    calls were nested inside #ifdef WIN -- dead on the Qt build since
    the port's first day (items 39/54's exact shape) -- so the Set
    Chart Info dialog's Time Changes list was always empty, its rows
    leaking to stdout; the dead branches also carried two latent flow
    bugs (a continue past each zone's rows after its header, and a
    continue where Windows does goto LSkip, skipping the Prev-state
    update), both superseded by the unified code. Probe first
    measured lookup rows=1, tzchanges rows=0; after, rows=174. New
    suite group atlas-sink (7 assertions), proven to fail with the
    bug reintroduced. Nets: 9-case console -N differential
    (byte-identical; two first-draft cases were degenerate -- a
    single-match city makes the count moot, and Nearby reads
    coordinates, not the location string); Wine-driven Set Chart
    Info screenshots for all three buttons, pixel-identical old vs.
    new, pinned date because the shot embeds the whole chart. One
    probe lesson for QT_TESTING.md-style work: printf argument order
    read the row counter before the call under test ran -- sequence
    the call, then print. Suite 3046/0, audits clean.

109. **P9/G4: the port's window state lives in one struct.** The ~40
    mutable file-scope statics qtdriver.cpp had grown -- tracking
    arrays, text window, sync'd menu actions, animation timer,
    resize flags, the network manager -- are a single QTUI struct
    (instance `qi`) at the top of the file, the port's analogue of
    Windows' Win32-only WI. Fields keep their site comments; CONST
    tables stay beside their code; gotcha 7's by-value lookup rule
    is written on the struct holding the arrays it protects. 219
    renamed sites, file-local by construction (statics). Nets: suite
    3046/0, text captures byte-identical, pinned-date 24-chart
    graphics captures byte-identical old vs. new, live windowed
    scenario. Capture lesson recorded: unpinned QTGRAPHDIR renders
    charts of *now* -- two runs of the same binary differ in 23 of
    24 charts, so a graphics differential must pin -qb on the test
    binary's command line.

110. **P8 closes tranche 3: D3 documented, D4 measured.** The print
    pipeline's modal contract is in CONVENTIONS.md "Output machinery"
    now -- is.S ownership, the is.nHTML 0/1/2/3 states, AnsiColor's
    off/ANSI/HTML tri-state -- each state read from the code before
    being written down (the 3 state exists solely so the first
    AnsiColor after the HTML header skips its closing </font>). D4's
    shared flag<->mode table idea is measured onto A3 with its
    boundaries: the Qt port's 35-row table plus Windows'
    ProcessState() longhand copy plus DetectGraphicsChartMode()'s 18
    else-ifs would collapse (row order carrying priority, rcBiorhythm
    a special case); PrintChart() stays -- byte-sacred sequential
    order, heterogeneous bodies, the FOutputSettings no-go shape.
    With P8, phase 2's tranche 3 is complete: P5-P10 all carry
    done-notes or measured verdicts.

111. **A3 taken: the flag<->mode mapping exists once.** `rgchartmode[]`
    in xscreen.cpp pairs every chart mode with the us.f* flag that
    selects it -- the 16 detection rows first, in the old else-if
    chain's priority order, `cchartmodeDetect` marking the boundary,
    and the GUI-only modes (gAspect..gCredit, which console builds
    don't define) guarded `WIN || QT`. Three longhand copies collapsed
    onto it: `DetectGraphicsChartMode()` scans it, keeping two specials
    (-HA detects as an aspect grid at -g's priority slot; rcBiorhythm
    is a value test on us.nRel, not a flag); Windows' `ProcessState()`
    looks its flag up in it, keeping the `ClearB` ranges since those
    also clear fAtlasLook/fZoneChange, which no row owns, and falling
    back to `gi.nMode = mode` for the five projection modes that never
    had a flag; and the port's private `rgchartmodeQt[]` is deleted.
    `PrintChart()` untouched, per item 110's verdict.
    - **Verified with pinned-time captures**: a HEAD-worktree build
      and the tree's build produce 24/24 byte-identical PNGs under
      `-qb`. The unpinned-capture trap (item 109 recorded it the day
      before: charts cast *now*, same binary differs 23/24) was re-hit
      and re-derived here because the lesson lived only in that work
      log entry -- it is in QT_TESTING.md's headless-rendering section
      now, which is where a session about to diff captures actually
      looks.
    - `TestChartModeTableQt()` (81 assertions, group `chartmode-table`)
      pins the row order, the mapping, detection priority, the
      specials, and the boundary. **Its expected table is a deliberate
      second copy of the mapping**: the first draft asked the table
      under test what order to expect, and the mandatory
      reintroduce-the-bug run -- two detection rows swapped -- passed
      163/163, the worthless-test trap again. The rewrite fails 3
      assertions on the same swap, and 3 more if the -HA special is
      dropped from detection.
    - One knowing nuance in the port: `SyncChartModeFromFlagsQt()`
      scans in table order, now detection-priority order rather than
      the old menu order. It only matters when a single typed command
      line turns on two chart-type flags at once, and the winner now
      agrees with what detection itself would pick.
    - Verified across builds: Qt suite 3127/0 (was 3046) and clean
      under ASan per QT_TESTING.md's procedure; console build compiles
      and renders a grid chart through detection; Windows build
      compiles and both win-*.txt scenarios pass under Wine.

112. **C2's net: the cast-cooking contracts are pinned.** CastChart()
    cooks the typed chart info in place inside ciCore -- LMT/LAT zones
    resolved from the longitude, auto-DST from is.fDst, the zone folded
    into the time, the latitude clamped off the exact poles -- and
    restores the typed values from a stack copy 350 lines later
    (calc.cpp:1260 -> 1616). REFACTORING.md's C2 wants that derived
    into locals, but net first: `TestCastCookingQt()` (group
    `cast-cooking`, 11 assertions) pins each cooked form against its
    explicitly-typed equivalent -- LMT vs. lon/15, LAT vs. the
    SwissLatLmt() equation-of-time zone, auto-DST against both is.fDst
    states, latitude 90 vs. the rDegQuad-rSmall clamp -- with exact
    equality on planet[oSun] and chouse[1], and asserts ciCore reads
    exactly as typed after every cast. All on a fixed 2020 date, so the
    suite's clock never enters (the item-109 trap).
    - Reintroduce-the-bug runs: deleting the `ciCore = ciSav` restore
      fails 5 assertions; cooking dstAuto to 0 instead of is.fDst
      fails 2. calc.cpp itself ships unchanged in this increment --
      byte-identical, CRLF intact.
    - The casts run under SetNoPopupQt(), because a pole cast may warn
      and a warning is a modal box nothing will click -- the
      TestBadInputQt() hazard, third time it has mattered.
    - Suite 3138/0 (was 3127), clean under ASan. C2's restructure
      remains open and is now unblocked; when it lands, this group is
      the net that has to stay green.

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
   slot keeps its body, and so its name. Hence the Name column, and hence
   Lookup Names offering `Sun/Moo` for a midpoint row.
   - **The glyph is the one part that does not keep**, since 2026-08-28.
     A slot forced to a midpoint kept drawing the glyph of the body it
     used to be, so a point the position list, the sidebar and this
     dialog all called `Sun/Moo` still had Chiron's glyph on the wheel.
     `DrawObject()` (xgeneral.cpp) now draws the three letter
     abbreviation for a slot with `force[obj] < 0.0`, the same fallback
     objects with no glyph of their own already use. Above the font
     paths, so it applies to every output format and not only the turtle
     vector one. Both builds, like the rest of this dialog.
   - Checked by rendering rather than by reading: change only the name
     and see whether the picture moves. **The sidebar has to be off for
     that to mean anything** -- it prints object names into the same
     image, so with it on a plain rename changes the picture too and the
     control proves nothing.
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
5. **Lookup Names fills the body field too, as number and name.** A
   looked-up row used to keep a bare catalogue number in Contains while
   the Name box beside it filled in, so the two columns disagreed about
   what the row was. The button now writes `10199 Chariklo` into the body
   field as well, for a plain MPC number with no point or flag suffix --
   the other definition forms have no number to pair a name with, and a
   suffix already occupies the space after it. Both builds.
   - **The parse had to learn that first.** A trailing run of letters
     after a space was always read as point/flag letters, so `Chariklo`
     would set the apsis marker off its own `a` -- measured, before the
     fix, as `pnt=4`. `FObjSelFlagRun()` makes a run count as flags only
     when *every* letter in it is one, so `10199 nH` still reads its
     suffix and `10199 Chariklo nH` reads both.

6. **The definition parse exists once.** Windows open coded it twice and
   this port had a third copy; `FObjSelParse()` in calc.cpp is the single
   one, and it keeps Windows' `if (pch > sz)` guard. Without that guard an
   all alphabetic definition reads its own letters as flags, so `Ven` sets
   the north node off its own `n`. `TestObjSelParseQt()` fails if it is
   removed.

**Testing it needs the real config.** `-i nrvate.as` sets `-Yi1 "/swe"`,
and `SwissEnsurePath()` caches the ephemeris search path on first use, so
a `-Yi` set afterwards does nothing. Without it every esoteric body reads
`???` and the dialog looks broken when it is not.

### HTTP without shelling out to wget

Astrolog needs to fetch exactly one thing: a body's positions from JPL
Horizons, for the `j<n>` custom object definition. Upstream does it by
`sprintf`ing a command line and handing it to `system()`:

    wget -q -O <file> "<url>"

which means an undeclared dependency on wget being installed, a shell
string assembled around a URL, no timeout, and a window frozen solid for
however long the network takes. `GetURL()` (io.cpp) has a proper
`URLDownloadToFile()` on Windows and that shell-out everywhere else.

The Qt build now uses **Qt Network**. That is not a new dependency: it is
the same Qt this build already requires, the same licence as the modules
already linked, and Qt is LGPLv3 against Astrolog's GPLv2-*or-later*, so
compatible. It matters that it speaks TLS — the Horizons URL is `https`,
so a plain-HTTP client would not have done, and vendoring a TLS stack for
one feature would be thousands of lines to maintain forever.

`FGetUrlQt()` (qtdriver.cpp) fixes each of the four problems: no shell, a
30 second timeout, a Cancel button, and an error that says *which* thing
went wrong rather than one "Failed to download" for all of them. It looks
synchronous to its caller because it has to — one caller is inside
`ComputeEphem()` and a chart genuinely cannot be drawn without the
position — but it runs a nested event loop, which is the difference
between a window that keeps painting and one the desktop offers to kill.
The progress dialog is suppressed when popups are off, so an unattended
run does not put up a box nobody is there to dismiss.

**The bigger win is the cache.** `GetJPLHorizons()` is called once per
object per chart cast, and nothing was cached, so a chart with three JPL
bodies hit a public NASA service three times on *every redraw* and
animating one hammered it continuously. Measured before: three identical
lookups cost 469ms, 367ms, 451ms. After: 473ms then 0ms, 0ms, 0ms, 0ms.

The key is the exact URL, which already encodes everything the answer
depends on — body, the three instants, center, topocentric or not — so
changing the chart changes the key and staleness is impossible. What is
cached is the **raw** reply rather than the finished numbers, because
`us.fTruePos` adjusts those afterwards and is not in the URL; verified
that toggling it still changes the answer (152.910217 against 152.925690)
rather than the cache flattening the two. A fixed 64-entry ring, so it
cannot grow without bound or reach the unfreed-allocation count at exit.

**The endpoint is the documented modern one**,
`https://ssd.jpl.nasa.gov/api/horizons.api?format=text&...`, rather than
the older `horizons_batch.cgi` upstream uses. The parameter names are
identical and `format=text` returns the same plain text, so the parse is
unchanged — verified by fetching both with identical parameters and
comparing: same target line, same CSV rows, same `$$SOE` marker.

**One connection, not one per body.** `QNetworkAccessManager` pools and
keeps connections alive *per manager*, so the manager is a session-long
singleton (`s_pnamQt`, torn down in `FinalizeQt()`). It was a local at
first, which threw that away and paid a fresh TCP connect and TLS
handshake for every object. `ASTROLOG_QT_NETLOG=1` prints where the time
goes, and answers it categorically rather than by stopwatch, because
Qt's `encrypted()` signal fires only when a handshake really happens:

    body 1  connect+TLS 130ms   done 1086ms
    body 2  connect+TLS reused  done  892ms
    body 4  reused                    919ms
    body 5  reused                    920ms

**Horizons is simply slow, and that is not something this end can fix.**
Look at where the time goes in those numbers: headers, first byte and
done are all the *same* timestamp. The reply is about 7KB, so the
transfer is instantaneous; the whole ~900ms is the server thinking before
it says anything at all. That is Horizons computing an ephemeris, and no
amount of client work touches it. The 130ms handshake — the only part
that was ever ours — is now paid once per session rather than once per
body, and the reply cache means a question already asked costs 0ms. Those
were the two available levers and both have been pulled. A first lookup
taking about a second is the service, not the program, and it is worth
knowing that before someone goes looking for a bug in this code.

**It is also a lesson in measurement.** An earlier attempt to demonstrate
the connection reuse by comparing wall-clock times across two runs showed
nothing at all — it even showed the reuse run's first call as *more*
expensive, which reuse cannot explain. With a 130ms effect sitting inside
a ~900ms server wait that varies run to run, and the run ordering
confounding what was left, the timings were noise. The signal, not the
stopwatch, is what settles it.

**One target per call is a hard limit**, confirmed in JPL's own API
documentation: `COMMAND` takes a single target, and when it matches
several things it returns a disambiguation list rather than several
ephemerides. There is no batching to be had. The server offers no HTTP/2
either — its ALPN advertises `http/1.1` only — and pipelining would not
help regardless, since each fetch completes before the next begins so
there is never more than one request in flight. Astrolog does already get
its three instants from one request rather than three, by asking for a
range with `STEP_SIZE`; that part was always right.

The console/X11 build still shells out, and the comment there now says
plainly what that costs. Windows keeps `URLDownloadToFile`, which at
least is not a shell, though it has no timeout either.

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

**A trap that used to come with those, now closed.** `case 'W':` in
astrolog.cpp was guarded by `#if defined(WIN) || defined(QT)`, so the
console build rejected that whole family as an unknown switch — which, as
the comment there says, stops it reading the rest of the file. The `-WM`
lines this change restores made that bite: a settings file saved from Qt
or Windows was not readable by the console build. The case is now
unconditional, the GUI-less branch consuming each switch's arguments and
discarding it, so a saved file loads everywhere. Item 32 is the story of
mistaking the old behaviour for a hang.

## Known divergences from Windows

Every place this port knowingly *differs* from Windows, so none of it
reads as an oversight later. Things the fork deliberately *adds* to both
builds are not divergences and live in their own section above. Anything
not on either list and not in an 8.x sub-item is unintentional — treat it
as a bug.

**Deliberately different behaviour**

- **Arrow keys in dialogs follow the layout, not the tab order.** Windows
  moves between controls with an arrow by walking the dialog's tab order
  within a group, and Qt does the same by walking its focus chain. Both
  give nonsense on a dialog whose controls are a 2D grid: in Object
  Restrictions the resource lists OK and Cancel before all 52 checkboxes
  and focus starts on OK, so Up wrapped around the whole chain to
  "Recall", at the opposite corner from the Cancel button directly above.
  This port picks the nearest focusable control in the direction pressed
  instead -- `PwArrowTargetQt()` in qtdialog.cpp, scored as distance ahead
  plus four times the sideways drift. Tab order is untouched and still
  matches the resource exactly. Controls that need the arrow keys for
  themselves (combo boxes, lists, text fields) keep them.

- **Animation is one switch, not two, and only the switch moves it.**
  Upstream stores the jump rate and the running state together in
  `gs.nAnim` -- magnitude is the rate, sign is on/off -- with `gi.fPause`
  a second independent stop on top. On Windows that means "Pause
  Animation" does nothing at all from a standing start, the master on/off
  is Shift+N, picking a jump rate can start the chart moving on its own,
  and "Reverse Direction" also starts it
  (`if (gs.nAnim < 0) neg(gs.nAnim)`, wdriver.cpp:2323). All four were
  reported here as bugs, by a user who had to be told which key actually
  starts it.
  - This port has **one** running/not-running state, behind
    `FAnimRunningQt()` and `SetAnimRunningQt()` in qtdriver.cpp. `p`
    starts it and stops it. "Do Animation" is the same switch under its
    Windows name, kept because the menu is checked against `astrolog.rc`.
    Picking a jump rate only picks a rate. Reversing only reverses.
  - The `gs.nAnim` encoding itself is unchanged, because the `-Xn` switch
    and saved settings depend on it (xscreen.cpp:1814). It is written
    down in one place now instead of open-coded at six call sites, which
    is what let three separate bugs through.
  - Stopped is one canonical state -- rate negated, pause clear -- so the
    two upstream stops can't disagree and leave the menu contradicting
    the chart.
  - `TestAnimationStateQt()` covers it; reverting the change fails ten of
    its assertions.

- **The IBM line drawing adjustment is not copied, on purpose.**
  charts1.cpp and general.cpp turn off `us.fAnsiChar` and switch the
  degree glyph while a text chart is drawn, whenever `gs.nFontTxt > 0`,
  under `#ifdef WIN`. That exists because Windows lets the text window use
  a font that has no box drawing characters in it. This port draws text
  charts in a fixed Liberation Mono and `gs.nFontTxt` does not change it,
  and that face carries every character involved -- U+2500, U+2502,
  U+250C, U+2510, U+2514, U+2518, U+253C and U+00B0 all report `inFont`
  true. Adding the guard here would strip line drawing from grids that
  render correctly, so it is a divergence that must stay. (The *graphics*
  side of `gs.nFontTxt` is not affected and does work, through `DrawSz()`
  and the Qt `DrawGlyph()`.)

| Where | Difference | Why |
|---|---|---|
| Display Settings | Raising "Number of Aspects to Include" actually un-restricts the newly included aspects | Windows assigns `us.nAsp = na` *before* the loop that would un-restrict them, so its loop can never run and raising the count silently does nothing. Kept the working version rather than reproduce the bug. |
| Chart info dialogs | Daylight shows and offers "Autodetect" | Windows resolves `dstAuto` via `DstReal()` before display, discarding the user's "work it out for me" choice on the next OK. Showing it survives a round trip. |
| Graphics Settings | Atlas City Coloring writes `gs.fLabelCity` | `DlgGraphics` writes `gs.fLabelAsp`, but that field is `-XA` (aspect glyphs on lines) and has nothing to do with city coloring. Treated as an upstream typo; using it would silently toggle aspect glyphs. |
| Command line dialog | Doesn't save/restore `us.fLoop`/`is.fMult` around the call | `CommandLineX()` does. Only matters for a typed line that itself starts a multi-chart sequence. |
| Restriction dialogs | (Since 8.9) checkbox = restricted, matching Windows | Previously "Show X" = visible, i.e. inverted. Flipped *toward* Windows, but it's a visible change to anyone used to the old Qt wording. |

**Not ported**

| Thing | Why |
|---|---|
| `wi.*` fields in File/Graphics Settings — bitmap-from-window, antialias level, no-popup, no-auto-redraw | Win32-only `WI` struct. |
| Wingdings, and the plain text families (Arial, Courier New, Consolas, Lucida Console, Cascadia Mono) | Offered in the font pickers, but not bundled — Wingdings is proprietary and the rest are system fonts. Qt substitutes when absent, as Windows does. |
| Open Charts in Folder skips Astrolog's own data files | Matches Windows, which does the same, since a folder of charts often sits alongside them. |

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
  failure. 2847 assertions covering dialog titles, the 42 context menus,
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
