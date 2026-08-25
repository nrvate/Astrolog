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

**Where things stand (2026-08-25).** All nine menus are built, all ~28
dialogs exist, and the item-8 UI parity sweep (8.1 through 8.14) is
finished — every *settings* dialog has been read field-by-field against
its `Dlg*` in wdialog.cpp and its resource block in astrolog.rc. The four
dialogs outside that sweep — chart list (`DlgList`), multi-chart info
(`DlgInfoAll`), command line (`DlgCommand`) and About (`DlgAbout`) — were
audited the same way on 2026-08-25; see item 14. Item 12, the missing animation loop, is also
done, as are Paste (9), the 96 macro slots (10), and Print (11) — so
everything on the original list is now ported. Item 13, chart-type
switches being ignored from the command line and macros, is fixed too.
Everything
knowingly left undone or deliberately diverged from Windows is recorded
either in the relevant 8.x sub-item or under "Known divergences from
Windows" near the end — if you find something undocumented, that's a
doc bug worth fixing, not a decision someone made silently.

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

## Prioritized remaining work

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

**Present but intentionally not editable**

Calculation Method, and Chart Settings' aspect sort and decan type, are
plain pick lists where Windows' are editable combos. Same choices, no
free-text entry. See 8.14.

**Not ported**

| Thing | Why |
|---|---|
| Graphics Settings' six font pickers | They pick from a hardcoded list of Windows GDI face names. Needs a real Qt font picker, not a translated list. |
| `wi.*` fields in File/Graphics Settings — bitmap-from-window, antialias level, no-popup, no-auto-redraw | Win32-only `WI` struct. |
| Graphics Settings' "Update Delay in Milliseconds" | Nominally the same reason, but really blocked on item 12 — there's no animation loop for it to set the delay of. |
| Open Charts in Folder skips Astrolog's own data files | Matches Windows, which does the same, since a folder of charts often sits alongside them. |
| JPL Horizons lookup blocks the UI | Synchronous network fetch inside the modal dialog. Windows does the same. Obvious async candidate. |

## Explicitly out of scope (don't implement)

- View > Window Settings submenu (Win32 resizable/bufferable window
  model, mostly meaningless for a Qt canvas that always auto-fits)
- Help > Setup submenu (Windows installer actions)
- File > Print Setup (native Windows print dialog)
- Right-click context menus (`menuV`/`menuG`/etc in astrolog.rc, starting
  ~line 607 — a separate resource set from the main menu bar, never
  investigated at all; worth a first look if right-click parity is ever
  wanted, but nothing here assumes anything about them)

## Working pattern / verification methodology

- Build with `make -f Makefile.qt -j4`; binary is `./astrolog-qt`.
- Compile-check after every change before testing live — this codebase
  has caught real preprocessor/linkage mistakes this way every session.
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
  sending keys/clicks. Prefer direct mouse clicks at measured coordinates
  over keyboard menu navigation (arrow keys inside an open popup menu were
  unreliable — looked correct, silently no-opped). **Never screenshot
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
- For crash debugging: the release `Makefile.qt` build has no debug
  symbols. Build a throwaway debug variant (`sed` a copy swapping `-O` for
  `-O0 -g`, changing `OBJDIR`/`NAME` to avoid clobbering the release
  build), then `gdb -batch -x script.gdb --args ./astrolog-qt-debug` with
  a script doing `run`/`print <globals>`/`bt`, driving the crash-triggering
  click via `xdotool` in parallel. Delete the debug artifacts afterward —
  not gitignored, not meant to persist.
- Git hygiene for this repo specifically: never put a `Claude-Session:`
  line in commit messages here (existing history was scrubbed of it once
  already at the user's request). `Co-Authored-By: Claude Sonnet 5` is
  fine. Create new commits, don't amend. The `qt` branch has no remote
  tracking branch and has never been pushed — `origin` points at the
  upstream `CruiserOne/Astrolog` repo, not a fork under this user's
  control.
