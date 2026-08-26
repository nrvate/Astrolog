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

**Where things stand (2026-08-25). Every item on this plan is done.**
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
   F-keys. 252 of the 275 non-macro accelerators are now bound.
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
   display and no `xdotool`, and exits non-zero on failure. **2728
   assertions** as of 2026-08-25; it was 1396 when first written, and has
   since grown to cover menu parity against `astrolog.rc` (257/257), the
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

**If upstream releases a new Astrolog**, note this fork's changes to
shared core are deliberately small and confined to `#ifdef QT` branches —
`grep -ln "ifdef QT" *.cpp *.h` finds them. Diff against the upstream
tarball rather than assuming; and keep line endings intact when editing
those files, which is its own trap (see "Working pattern" at the end).

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

### File — partially done
Done: Open Chart..., Save Chart..., Save Chart Positions..., Save Program
Settings..., Other Formats submenu (Save Chart Exchange/AAF, Save Chart
Quick*Chart, Save Chart iCalendar — done 2026-08-24, see
`ShowSaveAAFDialogQt()` etc. in qtdialog.cpp), Export Chart Text
Output..., Export Chart Bitmap..., Export Vector Format submenu (Metafile/
PostScript/SVG/Wireframe), Open Bitmap submenu (Open Chart Background,
Open World Map — also done 2026-08-24), File Settings... (done
2026-08-24, `ShowFileSettingsDialogQt()` — the portable subset of
`DlgFile`; skipped `wi.fBmpWindow`/`wi.nAntialias`/`wi.fNoPopup` as
Win32-only, and "Use Real System Fonts" since it needs the same Windows
GDI font enumeration the Graphics Settings font pickers would), Quit.

Missing (see "Prioritized remaining work" for how each maps to existing
portable functions):
- (Open Chart #2 is done — see Info below.)
- Other Formats `[P]`'s remaining two items: Open Charts in Folder... and
  Save Chart List... — both part of Chart List (see Info below), since
  they read/write `is.rgci`, which nothing populates yet; don't build
  standalone.
- Export as Wallpaper `[P]` (5 variants) — sets desktop wallpaper, a
  concept that doesn't map cleanly to modern Linux desktop environments
  (no single portable "set wallpaper" API the way Win32 has one). Lowest
  priority in File; consider just exporting the bitmap instead of trying
  to set wallpaper, or skip entirely.
- Print... `[D]` — no portable equivalent exists yet; could be built on
  `QPrinter`/`QPainter` rendering `gi.qim`, but nothing in the shared code
  does this today. Print Setup is a native Windows dialog — skip entirely,
  not applicable.

### Edit — done except Paste and macros
Done: Enter Command Line... (the escape hatch — see above), Copy Chart
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

Missing:
- Paste — needs clipboard read + figuring out what format(s) to accept;
  lower priority, no immediate need identified.
- 96 macro slots — lowest priority, deferred repeatedly across sessions.
  Only worth doing if specifically requested.
- 96 macro slots (8 submenus × 12 "Macro N" items, `cmdMacro01`..`96`) —
  explicitly low priority, deferred repeatedly across sessions. Only worth
  doing if specifically requested.

### View — done except one explicitly-skipped submenu
Done: Show Graphics, Colored Text, Redraw Screen, Set Colors..., Show
Interpretations, Print Nearest Second, Parallel Aspects, Applying Aspects.

Intentionally skipped: Window Settings `[P]` (Buffer Redraws, Clear
Screen, Hourglass on Redraw, Chart/Window resize-each-other toggles, Size
to Window/Full Screen, Scroll actions) — all about Win32's resizable/
bufferable window model; the Qt canvas already always auto-fits its
container, so most of these have no equivalent concept. If revisited,
"Clear Screen" and "Size Window Full Screen" (`gi.qwind->showFullScreen()`)
are the only two that might still mean something in Qt.

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

### Setting — COMPLETE
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
- Skipped as Win32-only (`WI` struct): animation update delay
  (`wi.nTimerDelay`) and "Don't Automatically Redraw Screen"
  (`wi.fNoUpdate`).
- Skipped as unportable: the six font selection combos
  (`gs.nFontTxt`/`Sig`/`Hou`/`Obj`/`Asp`/`Nak`), which pick from a
  hardcoded list of Windows GDI font names. Doing this properly means
  building a real Qt font picker, not translating a list — still open if
  anyone wants graphic-chart fonts on Linux.
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

## Work log — items 1-25

Kept because each entry records what was actually found, which is more
useful than the fact that it's finished. Several were not what their
original description said they were.

Items 1-15 are completed pieces of work. Items 16-25 are findings — how
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
   section above (font pickers deliberately left out).
7. ~~Info's multi-chart feature~~ — **done 2026-08-25** (chart slots and
   chart list both); see Info section above. Two paths in it remain
   unverified — see that section. With this the **menu structure is
   complete**: every top-level menu and dialog Windows has is present
   except File > Print (item 11) and the deliberate Win32-only omissions.
   **Item 8, the UI parity sweep, is complete (2026-08-25), and so is item
   12. Remaining: Paste (9), macros (10), Print (11).**
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
      via `ModifyMenu` on Win32-only `wi.hmenu`. Not ported; entries keep
      their default "Macro N" labels.
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
    and checks every item against the live Qt menu bar: **257 of 257
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
    unit conversion. **22 of the 23 dialogs** go through it.
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
    - `tools/rc_mnemonic_audit.py` checks all 848 label sites and is
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

## Known divergences from Windows

Every place this port knowingly differs, so none of it reads as an
oversight later. Anything *not* on this list and not in an 8.x sub-item
is unintentional — treat it as a bug.

**Deliberately different behaviour**

| Where | Difference | Why |
|---|---|---|
| Display Settings | Raising "Number of Aspects to Include" actually un-restricts the newly included aspects | Windows assigns `us.nAsp = na` *before* the loop that would un-restrict them, so its loop can never run and raising the count silently does nothing. Kept the working version rather than reproduce the bug. |
| Chart info dialogs | Daylight shows and offers "Autodetect" | Windows resolves `dstAuto` via `DstReal()` before display, discarding the user's "work it out for me" choice on the next OK. Showing it survives a round trip. |
| Graphics Settings | Atlas City Coloring writes `gs.fLabelCity` | `DlgGraphics` writes `gs.fLabelAsp`, but that field is `-XA` (aspect glyphs on lines) and has nothing to do with city coloring. Treated as an upstream typo; using it would silently toggle aspect glyphs. |
| Command line dialog | Doesn't save/restore `us.fLoop`/`is.fMult` around the call | `CommandLineX()` does. Only matters for a typed line that itself starts a multi-chart sequence. |
| Restriction dialogs | (Since 8.9) checkbox = restricted, matching Windows | Previously "Show X" = visible, i.e. inverted. Flipped *toward* Windows, but it's a visible change to anyone used to the old Qt wording. |

| Menu accelerator column | Reads `Shift+V`, `Alt+L` where Windows reads `V`, `Alt+l` | Astrolog's resource writes an uppercase letter alone to mean Shift. Qt derives the column from `QKeySequence` and spells the modifier out. Same keys, different notation; changing it means overriding how Qt renders shortcuts. |

**Present but intentionally not editable**

Calculation Method, and Chart Settings' aspect sort and decan type, are
plain pick lists where Windows' are editable combos. Same choices, no
free-text entry. See 8.14.

**Not ported**

| Thing | Why |
|---|---|
| `wi.*` fields in File/Graphics Settings — bitmap-from-window, antialias level, no-popup, no-auto-redraw | Win32-only `WI` struct. |
| Wingdings, and the plain text families (Arial, Courier New, Consolas, Lucida Console, Cascadia Mono) | Offered in the font pickers, but not bundled — Wingdings is proprietary and the rest are system fonts. Qt substitutes when absent, as Windows does. |
| Graphics Settings' "Update Delay in Milliseconds" | Nominally the same reason, but really blocked on item 12 — there's no animation loop for it to set the delay of. |
| Open Charts in Folder skips Astrolog's own data files | Matches Windows, which does the same, since a folder of charts often sits alongside them. |
| JPL Horizons lookup blocks the UI | Synchronous network fetch inside the modal dialog. Windows does the same. Obvious async candidate. |

## Explicitly out of scope (don't implement)

- View > Window Settings submenu (Win32 resizable/bufferable window
  model, mostly meaningless for a Qt canvas that always auto-fits)
- Help > Setup submenu (Windows installer actions)
- File > Print Setup (native Windows print dialog)
(Right-click context menus used to be listed here. They aren't out of
scope — they're the main thing left, and they've been promoted to "What
to do next" item 1 with what's now known about them.)

## Working pattern / verification methodology

- Build with `make -f Makefile.qt -j4`; binary is `./astrolog-qt`.
- Compile-check after every change before testing live — this codebase
  has caught real preprocessor/linkage mistakes this way every session.
- **Run the test suite before every commit.** `make -f Makefile.qt.test`
  then `./run-qt-tests.sh` — headless, no display needed, exits nonzero on
  failure. 2728 assertions covering dialog titles, the 42 context menus,
  263 shortcuts, 26 chart types rendering non-blank, all 337 menu items
  firing, 257/257 menu parity against `astrolog.rc`, and bad input. The
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
  dialogs (`tools/rc2qt.py`), the 42 context menus and the 848 menu
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
  menu mnemonics were verified rather than guessed.
  - Drive it on a **private** Xvfb display, never the user's desktop:
    `Xvfb :77 -screen 0 1200x900x24 &`, then `DISPLAY=:77 wine
    ./astrolog.exe &`. On a private display `import -window root` is fine
    and much easier than chasing window IDs.
  - **Qt needs a window manager for menus to open.** Under bare Xvfb the
    Qt app runs and renders, but Alt+mnemonic and menu-bar clicks silently
    do nothing and no popup window ever appears. `DISPLAY=:77 metacity
    --sm-disable &` fixes it. Wine doesn't need this — it manages its own
    windows — which makes the failure look app-specific rather than
    environmental.
  - **`xdotool key --window <id>` uses XSendEvent, which Wine ignores.**
    Keys appear to be delivered and nothing happens; captures then show
    the *previous* chart and read as a redraw lag. Activate the window and
    send via XTEST instead (plain `xdotool key`, no `--window`).
  - **Astrolog's accelerators are case-sensitive.** `v` is the Show
    Graphics toggle; `V` (i.e. `shift+v`) is Standard Radix. `Alt+l` and
    `Alt+L` are different commands. Send `shift+a`, not `a`.
  - Wine under Xvfb doesn't always repaint between commands. Forcing a
    resize away and back, then Redraw Screen (`space`), produced reliable
    captures where a plain sleep did not.
- **Don't `pkill -f` a pattern that matches your own command line.**
  `pkill -f astrolog-qt` kills the shell running it, which surfaces as a
  bare exit code 144 and no output — easy to misread as the app crashing.
  Use `pkill -x <exact-name>`, or match on a PID.
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
