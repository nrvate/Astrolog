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
2026-08-24, but don't trust it blindly if it's been a while.

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
5. **Any new chart mode added to `SetChartModeQt()` needs two edits, not
   one**: an entry in the big flag-clearing list at the top (or its
   `us.f*` flag will never turn back off when switching away), *and* a
   `case` in the switch (or the mode-specific flag never turns on, and
   downstream code that reads that flag directly — not just `gi.nMode` —
   silently misbehaves; this bit `gMoons`/`gExo` until caught, since
   `charts1.cpp`'s actual chart-casting logic reads `us.fMoonChart`/
   `us.fExoTransit` directly).
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
9. **Menu-checkmark staleness across independent entry points is an
   accepted, documented trade-off, not something to chase down.** E.g. the
   House System submenu doesn't resync if Calculation Settings changes
   `us.nHouseSystem` (Windows' own `DlgCalc` doesn't call `WiCheckMenu` for
   that either — matches upstream). One exception was fixed because it was
   cheap and one `QAction`: `SyncHelioMenuQt()` refreshes the Heliocentric
   checkbox since Calculation Settings can change the central planet from
   there too. Don't build a general `RedoMenu()`-style full resync unless
   a specific new case actually needs it.

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

### Info — done except the chart list
Done: Set Chart Info..., Chart for Now, Default Chart Info..., all 8
relationship chart type radios, and (2026-08-24) Set Chart #2 Info...,
Charts #3 Through #6... (`ShowChartsAllDialogQt()`, equivalent to
Windows' DlgInfoAll), and Swap Chart #1 and #2 inside the Chart List
submenu.

The six **chart slots** are done: `rgpci`/`rgpcp` (extern.h, `cRing = 6`)
are the per-ring chart info/positions, slot 1 being the main chart and
2-6 the extra rings a bi/tri/.../hexa wheel draws.
`ShowChartInfoForQt(CI *, title)` edits any slot;
`FOpenChartIntoQt(iChart, file)` loads a file into one (FInputData()
always lands in ciCore, so for slots >1 it saves ciCore, reads, copies
out, and restores -- the same dance Windows' DlgOpenChart does).

Missing — the **chart list**, a separate thing from the slots above and
the one remaining chunk of this feature:
- Chart List... `[D]` (`DlgList`, wdialog.cpp:866, ~150 lines: list view
  with filtering and sorting), Previous/Next Chart, First/Last Chart.
  These read `is.rgci` (the loaded chart list) / `is.cci` (its count) /
  `is.iciCur` (current index). Navigation itself is trivial — see
  wdriver.cpp:1545-1564, it's a clamp and a `ciCore = is.rgci[i]` — the
  work is the dialog. A Qt version is probably a `QTableWidget` over
  `is.rgci`.
- File's Open Charts in Folder... and Save Chart List... belong to this
  same chunk (they populate/write `is.rgci`); Save Chart List is just
  `us.nWriteFormat = 'l'; FOutputData();` once a list exists.
- **Nothing populates `is.rgci` in this port yet**, which is why the
  above is deferred as a unit rather than half-built.

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
7. Info's multi-chart feature — **half done 2026-08-24**: the six chart
   slots (Set Chart #2 Info, the Charts manager, Open Chart #2, Swap) are
   in; see Info section above. **Next item up** is the remaining half, the
   chart list: the `DlgList` port (wdialog.cpp:866, ~150 lines) plus
   Previous/Next/First/Last and File's Open Charts in Folder / Save Chart
   List. Read `DlgList` in full before starting — the navigation is
   trivial, the dialog is the work.
8. **Edit menu Paste** — needs clipboard read + figuring out what format(s)
   to accept; lower priority, no immediate need identified.
9. **Edit menu's 96 macro slots** — lowest priority, deferred repeatedly.
   Only do this if specifically asked.
10. **File > Print...** — no existing portable rendering path to a
    printer; would need new code built on `QPrinter`. Not investigated.

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
