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

3. ~~A regression check~~ — **done 2026-08-25.** `make qt-test &&
   ./run-qt-tests.sh`. Runs headless in seconds, no X display and no
   `xdotool`, and exits non-zero on failure. It was 1396 assertions when
   first written and prints its own count now — the only number that
   stays right, after three documents asserted three different wrong
   ones. It has grown to cover menu parity against `astrolog.rc` (258/258), the
   Chart menu's graphics/text handling, bad input, and — since item 141
   — whether the computed positions are actually right.
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
5. **~~Decide about the deliberate divergences.~~** — **resolved
   2026-08-30** (work log item 132), and not the way this item expected.
   Rather than decide whether to keep five divergences, they were
   *tested*, and testing them settled three by itself. Display Settings'
   aspect count turned out to have silently reverted to the Windows bug
   months earlier — fixed in both builds now, so it is no longer a
   divergence at all. Atlas City Coloring turned out to be documented
   upstream (`-XL[1-5]` colours cities "when -XA is on"), so the row
   describing it as a typo was wrong and its proposed fix would have
   been a bug. The rest keep their behaviour and now name the test that
   holds it; only the command line dialog's save/restore is untested,
   and it says so. The standing rule is at the head of that section:
   **a divergence without a test is a divergence waiting to revert.**
   The one a user met on every menu — the accelerator column reading
   `Shift+V` where Windows writes `V` — **is fixed**, see item 44, so this
   item no longer has anything urgent in it.
6. ~~**Unfinished business, low value:**~~ — **closed 2026-09-02 at the
   maintainer's direction**, without work. Wingdings and the plain text
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
   outranks everything above it. **Second session run 2026-08-30**
   (work log item 114): the maintainer's own 7.40-era config and real
   chart files, driven through the live GUI. It found a shared-core
   crasher (a text-chart buffer overflow on atlas-length location
   names), silent rot in the GUI-automation driver, and a file-dialog
   parity gap — none visible to the 3000+ mechanical assertions.
   **Third session 2026-08-30** (work log item 131): found Display
   Settings' aspect count to be a one-way ratchet — lowerable, never
   raisable — a documented divergence that had silently reverted to the
   Windows bug months earlier. Two ASan sweeps over every real chart
   came back clean, so the yield was again in the GUI rather than the
   calculation core. The
   item stays open on purpose: it is a practice, not a task. Every verification before that date was
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
   **"All fixed" was wrong, and stayed wrong for five days.** The whole
   `-b` backend sub-family was still dropped -- `nSwissEph`,
   `fPlacalcPla`, `fMatrixPla`, `fPlacalcAst`, `fMatrixStar`, none of
   them written -- so choosing Moshier or JPL and saving gave Swiss back
   in silence. Fixed 2026-08-31 (work log item 140), found by the numeric
   oracle rather than by looking. Two lessons worth more than the fix:
   `registry_audit.py` cannot see this class, because it checks that
   every spelling the program *writes* resolves to a row, not that every
   setting *gets* written -- the missing audit is the other direction.
   And a struck-through item is a claim like any other; this one had been
   read as settled by everyone who passed it, including C4's survey,
   which documented the five fields' encoding on 2026-08-29 without ever
   asking whether they were persisted.
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

10. **~~The T2 enforcement campaign.~~** — **finished 2026-08-31.**
   E1-E4 tagged the families that had shipped cross-domain incidents
   (items 125, 127-129), and O1-O3 range-guarded the object core
   (items 135-137) after measuring that a tag would not have caught any
   of the incidents there. REFACTORING.md's "T2 enforcement campaign"
   section carries the whole ledger, both mechanisms, and the four
   verdicts that closed the rest.
   Two things are open there on purpose, and neither is a task:
   **domain tagging the object core** waits on a cross-domain incident
   in that family, of which there has never been one; and the
   **`Mem` storage arrays** are never subscripted, so a guard would
   check nothing. Both say so at the ledger row.

11. ~~**Extend the oracle.**~~ — **closed 2026-09-02** (work log items
   153, 157-158, 171). Added 2026-08-31 (work log item 141). The
   numeric oracle covers planetary longitude and house-cusp ordering, and
   that is all. Aspects, midpoints, progressions, eclipses, returns, the
   atlas and the interpretation text still have no reference outside this
   repo, and REFACTORING.md's T9 makes the argument for why that matters:
   every other net here is differential and can only say "unchanged".
   ~~The cheapest next ones need no external data at all — the
   invariants the program must satisfy whatever the numbers are.~~ —
   **those four are done**, 2026-09-01: legs 6-9 in `TestNumericOracleQt`
   are aspect symmetry, a midpoint lying halfway between its sources, a
   progressed chart at zero elapsed time equalling the natal, and a
   return chart's object actually being at its natal longitude. Leg 4b
   added the house-partition invariant across latitude *and* engine,
   which is what caught the five degenerate systems.

   ~~**What still has no reference outside this repo**: eclipses, the
   atlas, and the interpretation text.~~ — **all three done 2026-09-02,
   work log item 171.** Legs 10-12: 60 solar and 40 lunar eclipses
   against the Swiss library's own eclipse finder plus 55 midpoints where
   there must be none, the atlas's nearest-city search against a
   brute-force scan, and the interpretation tables against the shape they
   promise. The third found a live segfault reachable from the command
   line.

   **This item is closed**, and its own next step was taken too: legs
   13-15 (work log item 177) do the same for the three *search*
   functions — `ChartInDaySearch`, `ChartTransitSearch` in its ordinary
   mode, and `ChartHorizonRising` — each checked against the condition it
   was searching for. Two of the three had no coverage at all before.
   ~~**Both findings below are confirmed, and there are five more**~~ —
   **fixed 2026-09-01, work log items 157-158.** (An intermediate note
   here said they did not reproduce; that was measured at a single date
   and longitude and was wrong. Item 153 carries the retraction.) Seven
   systems degenerated toward the pole; reading them split two classes.
   **Topocentric, Campanus, Regiomontanus, APC and Savard-A** returned
   cusps whose gaps summed to 1080–3960 degrees rather than 360 — those
   fall back to Porphyry now, via a post-condition
   (`FEnsureHousePartition()`) called from **both** engines, since five
   of them fail on the Swiss path the old guard never reached.
   **Sunshine** was fixed too (item 160): its zero-width house turned out
   to be a polar guard the Swiss library's author wrote and commented out
   in both Sunshine solutions, in a file that falls back to Porphyry for
   Placidus, Koch and Regiomontanus. Only **Pullen (S.Delta)** collapses
   on purpose, in Astrolog's own `HousePullenSinusoidalDelta()`, and it
   is the one system leg 4b still expects to.

   Two measured findings are parked there rather than fixed, both
   maintainer calls because they change house math in both builds:
   **Topocentric houses run backwards beyond the polar circle** (at 78N
   the twelve cusps wrap the circle three times, so house assignment is
   meaningless) while Placidus and Koch are guarded and fall back to
   Porphyry; and **Pullen (S.Delta) produces zero-width houses** at 70N
   and above. Longyearbyen at 78.2N is in the shipped atlas.

12. ~~**Finish T5: the formatting calls that write into a caller's
   buffer.**~~ — **done 2026-08-31** (work log items 143-145). Thirteen
   functions that format into a destination they did not own became one,
   and that one (`WchToUTF8`) is bounded by construction with a comment
   saying so. 1,141 of 1,231 formatting calls are bounded; the 90 left
   are pointer arithmetic, struct members and split lines, none of them a
   caller-owned buffer. The last increment found two live stack smashes
   in shared core, `astrolog -YYt` and `-YXt` with a long argument, both
   fatal in the release build and both in the Windows one too. `sizeof` there is 8, so the mechanical conversion would
   truncate every string to seven characters *while compiling clean* --
   they need a size threaded from their callers, one function at a time,
   which is real work rather than a sweep. They are also the sites most
   likely to overflow, since a function formatting into someone else's
   buffer is exactly the case where nobody owns the bound.
   `tools/chart-matrix.sh` and `tools/switch-matrix.sh` make each one
   provable. The 11 pointer destinations and the 78 using pointer
   arithmetic or struct members are the same job, lower risk.
   `wdriver.cpp`/`wdialog.cpp` were left out of the sweep entirely --
   upstream-shaped, and neither matrix can exercise them.

13. ~~**Work the warning ledger down.**~~ — **closed 2026-09-01** (work
    log items 146-151). Nothing in this project had ever read a compiler
    warning; `tools/warning_audit.py` now holds all four builds against
    `tools/warnings.txt` and fails on any addition. **857 warnings in 209
    sites down to 318 in 101 at the campaign's close, and every class
    still in it carries a recorded verdict** — so the next warning to appear here will be a new
    one. Separately, and this is the number a person actually sees:
    **all four builds now compile silently** (items 151-152). A console
    build was 49 warnings in 722 lines of output and is 0 in 32. The
    audit uses `-Wall` and an ordinary build does not, so the two counts
    answer different questions — everything left in the ledger is
    invisible to `make`.

    What the campaign actually found, in rough order of how much it
    mattered:

    - **`make -f Makefile.win` had not compiled for 62 commits** (item
      146). `-w` was hiding a hard error, and three work log items had
      listed "Windows builds" among their nets meanwhile.
    - **Clear Screen did nothing in text mode** (item 149), found by
      following a `defined but not used` to a window the port stopped
      creating.
    - **A truncated atlas silently re-parsed one line 33,219 times** and
      blamed the time-zone rules (item 149).
    - **A pointer compared to itself**, leaking every `-YIC` string, and
      two provable buffer overflows (item 147).
    - **Thirty dead locals**, every one residue rather than a forgotten
      computation (item 148) — and `tools/graphics-matrix.sh`, built to
      prove it, which is now the only differential over the drawing code.

    And what it deliberately did **not** do: `-Wmaybe-uninitialized` (91)
    got a measurement instead of ninety-one edits — GCC proved zero
    use-before-set in the whole tree, and 23 of the warnings appear at
    one optimization level and not the other, which is an analysis
    artifact rather than a defect. Initializing those to silence them
    would have converted latent bugs into confidently wrong answers.
    Eight of them are a worklist for REFACTORING.md's T7 instead.

    The tail of it is worth repeating because it is a lesson about nets
    rather than about warnings: the campaign spent five increments on a
    ledger built with `-Wall`, and the maintainer's build — which has no
    `-Wall` — barely moved until somebody measured *it* (item 151). A net
    that sees more than the thing you are trying to improve is not
    automatically a net for it.

14. ~~**The build system, and packaging.**~~ — **done 2026-09-01** (work
    log items 167-169), on the maintainer's ask. Six defects, and two of
    them could hand you a binary that did not match its source: header
    dependencies were tracked by hand, so touching `placalc.h` rebuilt
    **zero** objects in every build; the console makefile named no C++
    standard, which is the accident `Makefile.win` had already died of;
    plus a parallel-build race, a `make clean` that cleaned a third of
    the tree, an unused Qt Test module, and the source list written out
    five times. `Makefile.srcs` holds it once now, and the four Linux
    binaries came out byte-identical afterwards, which is what makes that
    provable rather than argued.

    Then `make install`, shaped by the maintainer's answer to where the
    data goes: it stays in the checkout, and what installs is a wrapper
    that runs the in-tree binary, plus a menu entry and icons. On the way
    to the desktop entry: **the Qt window had never had an icon at all**,
    where Windows sets one from `astrlog1.ico`. And `make qt6` /
    `make qt6-test`, so the Qt6 build is a target rather than an artifact
    somebody remembers making.

    **Still open, and it has its own document: CI.** `QT_CI_PLAN.md`
    covers putting these builds, the suite, the audits and the release
    artifacts under GitHub Actions. Nothing here runs on push, which is
    how `Makefile.win` went 62 commits without compiling.

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

Diff against the upstream tarball rather than assuming. The line-ending
trap that used to sit here is gone: the tree is LF throughout since work
log item 159, pinned by `.gitattributes` and checked by
`tools/line_endings_audit.py`. Upstream's tarball is not, so expect the
diff itself to be noisy.

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
         newly included aspects. Qt runs both loops before the
         assignment, so it works. Keeping the working version rather
         than reproducing the bug — flagged here so it reads as a
         choice, not an oversight.
         **This note was false for months** (work log item 131): the
         un-restrict loop was deleted by the transcription pass in
         commit `bf92b9e`, and the port silently reproduced the Windows
         bug — the count could be lowered and never raised. Restored
         2026-08-30, and `TestAspectCountQt()` now drives the dialog
         both directions so the claim above is checked rather than
         asserted. A divergence documented but not tested is a
         divergence waiting to revert.
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
    - Three things were left open in this family and **all three closed
      later** (verified 2026-08-31, or this would still read as pending
      work): the open-coded definition *formatter* became
      `SzObjDefFormat()` (item 59), the lookup's type-to-name switch
      became `SzObjSelName()` with the JPL Horizons web case folded in,
      and the glyph rule moved into `ObjDefSet()`, which the Custom
      Objects dialogs now store through — so a redefined slot drops the
      old body's glyph there too.

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

113. **C2 taken, in the shape the code could take.** The full
    derive-into-locals the finding imagined is a measured no-go, with
    the evidence at the finding: cooked TT is read by
    ComputeVariables() and by AstroExpression hooks firing inside the
    cast (funTim/funDst/funZon/funLat read the CI macros), and clamped
    AA is a global input to a dozen matrix house functions. Taken
    instead: the ciSav pair is `Borrow bciCore(ciCore)` (any future
    early return restores -- the finding's recorded hazard), the
    typed/cooked contract is a comment at the cooking site naming
    every cooked-state reader, and one undocumented in-window
    corruption the audit turned up is gone.
    - **HouseTopocentric() smuggled radians through ciCore.** It
      passed its Polich-Page pole latitudes to CuspTopocentric() by
      assigning them to AA -- the user's typed latitude -- five times
      per cast, in radians, in a field whose unit is degrees,
      restoring after. Internally consistent, so no output bug, but
      the maintainer flagged it and it fell squarely under C2:
      CuspTopocentric() takes the pole as a parameter now and AA is
      written by nothing but the documented clamp.
    - **Nets**: byte-identical -v output across all 40 house systems
      (Swiss) plus 7 Matrix-backend runs, old worktree binary vs.
      new, pinned -qb chart. Switch trap recorded: `-bm`'s handler
      tail toggles fEphemFiles back on, so `_b -bm` quietly re-enables
      the ephemeris and diffs Swiss against Swiss; plain `_b` is how
      to reach the Matrix house path.
    - **A standing pin for matrix.cpp**, its first: the topocentric
      cusps at literal constants under the Matrix backend
      (cast-cooking group). It held alone and failed in the full
      suite -- TestAllMenuActionsQt fires all 338 menu items and
      leaves fFlip, fGeodetic, objOnAsc, objRot1/2 and szExpHou
      dirty behind it. Found by bisecting groups, then byte-diffing
      `us` before/after each fired action and mapping offsets to
      fields with `gdb -batch -ex 'ptype /o US'` on the ASan binary
      (it has -g; the test binary doesn't). The pin now borrows
      every cast-relevant knob it depends on, and fExpOff besides,
      which silences the hook class wholesale.
    - Falsified twice: swapping the P1/P2 poles fails the pin (in
      the full suite, where it has to hold); the item-112 sabotages
      still fail their assertions. Suite 3139/0 twice over, clean
      under ASan; win build and scenarios clean. CONVENTIONS'
      one-Borrow-per-deduced-type rule bit once, exactly as written.

114. **The second real-data session: the maintainer's config, the
    maintainer's charts, the live GUI.** Item 7's practice, executed
    with /data/med/astrolog.as (a 7.40-era settings file) and the real
    chart files beside it, driven through qtdrive plus console runs and
    a Wine cross-check. Verified working end to end: startup render
    with that config, chart-mode switching, a live Placidus recast off
    the House System menu, Set Chart Info carrying the loaded chart's
    data, an atlas lookup of "Austin, TX" returning real rows, Open
    Chart #2 through the native file dialog, the comparison wheel with
    both charts' sidebar blocks, Set Chart #2 Info, the Transits
    dialog, a .dat event chart, text and bitmap export to real files,
    and File/Exit returning status 0 four times out of four.
    - **A shared-core crasher, the session's reason to exist.**
      Exporting the text of a saved lunar-eclipse chart aborted the
      process: fortify caught `PrintHeader()` (charts1.cpp) formatting
      the chart's location plus seconds-precision coordinates through
      an 80-byte buffer — the file's atlas-produced location name is
      58 characters by itself ("Washington, Washington West, District
      of Columbia Co., DC"). The same pattern sat in the ciTwin block
      below it and in `PrintWheelCenter()`. All fixed with cchSzLine
      buffers plus `sprintf2` per CONVENTIONS' bounded-formatting
      rule; charts2's relation header was checked and is safe (its
      format string carries a precision). Nets: the console repro now
      completes byte-identical to the intended output, pinned-time
      text and graphics captures are byte-identical old binary vs.
      new, and a shared-core-fixes assertion drives both functions
      with 120-character names and locations. **Falsification took
      two attempts**: reverting only the buffer passed, because
      sprintf2 still bounds it — the fix is two layers and only
      reverting both (buffer and sprintf) reproduces the abort. The
      crash reaches Windows too (same sprintf, no fortify — silent
      stack corruption there), so the fix ships in both builds,
      unguarded.
    - **The GUI-automation driver had rotted silently.** Since the
      accelerator work, a QAction with a shortcut reports its
      accessible name with the shortcut appended ("Open Chart...\t
      Alt+o"), so qtdrive's by-name lookup missed every
      shortcut-bearing menu item — scenarios/objectsel.txt still
      passed only because that one item carries no shortcut. Also any
      label containing '#' ("Open Chart #2...") was misrouted into the
      positional role#n branch. Both fixed in tools/qtdrive.py, which
      also gained `key` and `typeraw` commands — the escape hatch for
      the native GTK file dialogs AT-SPI cannot see into (Ctrl+L, type
      the path, Return).
    - **A parity gap fixed**: both Open Chart dialogs now carry
      Windows' seven-filter list (.as, .aaf, .qck, .xml, .ics, Solar
      Fire .txt, all files) instead of .as-or-everything. The save
      dialogs already matched Windows' two-filter save side.
    - **Recorded, not changed**: the default astrolog.as is searched
      for in the executable's directory before the current directory
      (io.cpp FileOpen, upstream order), so running the repo binary
      from a directory with its own astrolog.as silently loads the
      repo's — reach a personal config with -i. The Chart menu shows
      "Alt+T" for Transits but the .rc accelerator table binds
      Shift+Alt+T there and gives Alt+T to the sidebar toggle — an
      upstream label/accel mismatch this port reproduces exactly
      (proven live in both builds; a windrive alt+t blanked the Wine
      sidebar before the shot, which is also why that screenshot
      confused a whole debugging pass). And the Transits dialog's
      initial fields are ciTran, frozen before command-line switches
      in both builds — with a config loaded via -i its zone shows the
      compiled default, and the dialog's Now button, which reads the
      live ciDefa, is the correct path.
    - The Wine cross-check also confirmed the two builds render the
      same wheel for the same chart and config, sidebar included.

115. **The long-strings battery, and the three star bugs it flushed
    out.** Item 114's crasher class -- a user string through a fixed
    line buffer -- is now pinned across the whole text chart surface:
    the `long-strings` group renders every mode in rgchartmode[] to a
    file with 120-character chart names and locations in place, 1.6s
    for all 35. Falsified by reverting both layers of the PrintHeader
    fix: the battery dies on the first vulnerable mode.
    - **Its first full-suite run found a different bug family.** Alone
      the group passed; after the full suite -- which leaves stars
      unrestricted -- ASan reported a global-buffer-overflow, and then
      two more on successive runs, all upstream-inherited reads of
      object-keyed tables (sized oNorm+1) by star indexes:
      ChartInfluence()'s -j0 stanza crediting `power1[rgObj1[star]]`
      (in upstream's nine copied stanzas too, D2's rework preserved it
      faithfully); InterpretEsoteric() reading rgObjRay[] where its
      neighbor five lines up already carried the i<=oNorm guard; and
      NCheckEclipseSolar()/Lunar() reading rObjDiam[] raw where their
      sibling RObjDiam() has always used FNorm(). Fixes follow the
      guards upstream itself used at Dignify() and charts1.cpp:1785.
    - **The influence one crashed outright**: `=U -j` (stars
      unrestricted, influence chart) bus-errors the pre-fix binary on
      every run and completes with sane 100.0%-total output after.
      Two debugging traps cost real time and are worth knowing: the
      crash is address-layout-sensitive, so it vanished under gdb and
      under -O0 and -g rebuilds -- ASan on the suite was the reliable
      witness -- and one "new binary still crashes" scare was a stale
      root-directory .o; the console build objects live in the repo
      root and do not rebuild just because the Qt objects did.
    - Uncovered on purpose, recorded here: the battery does not vary
      the -I interpretation axis, us.fSeconds beyond on, or
      relationship modes; InterpretLocation()'s rgObjRay[i] read at
      intrpret.cpp:293 is reachable only for objects with szMindPart
      text and stars have none, so it is guarded in practice but not
      in code.
    - Nets: suite 3176/0 (was 3140), clean under ASan; pinned text and
      graphics captures byte-identical for default (star-restricted)
      charts, so nothing changed for anyone not running `=U`; all four
      builds compile; win scenarios pass.

116. **B2 taken: the virtual filenames exist in one table.** The
    plain-copy names FInputData() accepts in place of a real file
    ("set", "__t", "__g", "__d") are one commented rgvirtfile[] table,
    with the three behavioral specials (nul, now, tty) and the
    "__1".."__6" slot pattern documented beside it, and -H's -i entry
    now lists every virtual name -- indented four spaces, past where
    registry_audit reads switch tokens, and verified clean against all
    six audits. Behavior identical by construction; the -i spot checks
    (nul, set, now, __d) and the full suite are the net.

117. **The menu sweep stops leaking the user's macros into the suite.**
    Three groups (custom-parse, objdef-set, objsel-glyph) went red with
    no code change at all: their preconditions say nobody has touched
    custom slot 1, and TestAllMenuActionsQt() fires the user's own macro
    menu items, several of which are `-M0 "-i /data/med/*.dat"` loads
    full of `-Yeb` redefinitions. Whether that dirties the slots
    depended on whether those files exist *outside the repository* --
    they had been missing from the macro paths on this machine, so every
    earlier run's macro loads failed silently and the suite stayed
    green; the moment the user restored them (reorganizing /data/med
    while the suite was running, as it happened) the sweep started
    really redefining slot 1 and the three groups reported on it. The
    sweep now snapshots the custom slots' identity (all four
    `rg*Swiss[]` arrays, both glyph tables, `szObjDisp`) and restores it
    after firing -- text, not pointers, for the strings, since a
    redefinition frees the clone a saved pointer would name, the same
    szObjDisp discipline TestObjSelGlyphQt() already documents.
    Diagnosed by bisecting group subsets down to menu-actions, then a
    temporary per-item probe naming the exact culprit (macro "YEBSet1").
    Two lessons written into QT_TESTING.md: the suite must not depend on
    files outside the repo, and two suites must never run concurrently
    (their shared $TMPDIR fixtures corrupt each other, which spent an
    hour looking like a regression). One loose end, recorded so a
    resurfacing has a starting point: a single full-ASan run in the
    leaking state reported a global-buffer-overflow, with no stack
    captured, that no run since the restore reproduces — if it comes
    back, start from a custom slot redefined by macro to a high
    asteroid number.

118. **B1's net: the import parsers' long-line behavior is pinned, and
    building the net caught five crashers.** REFACTORING.md B1 says the
    six file-format readers drift and to pin their truncation points
    with fixtures before consolidating them. The file-parsers group (11
    assertions) fixture-loads all five import formats through
    `FInputData()` plus a 2000-character switch-file line, and pins
    what each reader does past its own limit: a calendar SUMMARY keeps
    its first 246 characters; a Solar Fire name line past 254 poisons
    the whole file (its tail is eaten as the date line, so range
    validation rejects everything); an AAF line past 1019 splits and
    the tail's missing '#' rejects the file; the switch reader's
    realloc growth delivers long lines whole. None of that was written
    down anywhere before, and the consolidation must not move any of it
    by accident.
    Writing the fixtures crashed the program four ways before pinning
    anything — all upstream-inherited, all reachable from a user's file
    or command line, each found by the probe loop then frozen as a
    regression case:
    - `FProcessAAFFile()` assembled name and location fields into a
      cchSzMax buffer with unbounded sprintf; any AAF field over ~250
      characters smashed the stack. Both assemblies are `sprintf2` now.
    - `FProcessADBFile()` joined city and country, each individually
      capped at cchSzDef, into one cchSzDef buffer. Also `sprintf2`.
    - `NParseSz()` and `RParseSz()` copied their argument into a
      cchSzMax local with an unbounded loop — reachable from every
      switch argument and import field in the program (`-m
      Febr<300 chars>` crashed from the command line). The copies stop
      at the buffer now, so an overlong token parses by its head.
    - `FErrorValR()` formatted the out-of-range value itself through
      `FormatR()` into buffers no big double fits — 1e308 in %f style
      is over 300 characters — so *reporting* a bad value was a second
      crash, surfaced the moment the fixed RParseSz returned an
      astronomical value intact. szVal is cchSzLine and the assembly is
      `sprintf2` now; pinned by a 400-digit `-q` argument in the
      bad-input group.
    Falsified the way the house rule demands: with the io.cpp fixes
    reverted the group dies under the fortify checks before its first
    assertion. Suite 3188/0 (+11 file-parsers, +1 bad-input), clean
    under ASan; the Windows and console builds compile the same fixes,
    and the console binary was crash-tested directly with the long
    arguments. B1's remaining halves — the reader consolidation and the
    SF/calendar 254-vs-1020 mismatch — are still open, now safe to take
    behind these pins.

119. **B1 taken: the import readers exist in two helpers.** The getc
    loop that AAF, Astrodatabank and the JPL Horizons response parser
    each hand-rolled is `FReadSzLineSkip()`; the fgets-and-trim loop
    that Solar Fire, calendar and (minus the trim) Quick*Chart
    hand-rolled is `FReadSzLineTrim()`, both in io.cpp above the switch
    file reader. Each caller passes its own buffer and limit, so the
    per-format truncation points item 118 pinned are unchanged --
    including, for now, Solar Fire and calendar reading cchSzMax
    characters into a cchSzLine buffer, the mismatch the finding led
    with, kept byte-identical here so its fix can be a decided change
    of its own. The switch-file reader stays hand-written on purpose
    (realloc growth is its own policy, documented at the helpers), as
    does FInputData's format-detection peek. Net: an 11-fixture
    differential -- every import format, control and long-line variants
    both, driven through the console binary before and after,
    byte-identical -- plus item 118's pins and the full suite at
    3188/0; the Windows build compiles the same sources. The rebuild's
    -Wformat-overflow noise also put atlas.cpp's zone-error paths on
    the record as the same overflow class (szLine formatted into
    smaller buffers, six sites); recorded at B1, deliberately not taken
    here.

120. **B1 closed: Solar Fire and calendar lines get their whole buffer.**
    Both parsers declare `szLine[cchSzLine]` and read it with
    `fgets(..., cchSzMax, ...)` -- a 1020-byte buffer fed 254 bytes a
    line, the mismatch B1 led with, which silently split every line
    between 255 and 1019 characters. The buffer's declared intent wins:
    both call sites now pass cchSzLine. Concretely, a 400-character
    calendar SUMMARY used to truncate at 246 and now arrives whole, and
    a 300-character Solar Fire name line used to poison its entire file
    (the split tail was consumed as the date line, so range validation
    rejected everything) and now loads. Past the real 1019 limit each
    format's old behavior still holds and stays pinned, by two new
    over-limit cases beside the updated ones. Net: 4 of the pins fail
    with the two-token change reverted; suite 3190/0; the 11-fixture
    console differential changes in exactly the two files it should
    (cal-long, sf-long) and is byte-identical in the other nine; the
    Windows build compiles the same fix. With this, B1 is closed:
    net (118), consolidation (119), and the decided mismatch fix, with
    atlas.cpp's zone-error paths left recorded at the finding as the
    class's known remainder.

121. **The atlas zone-error paths join the bounded-formatting rule.**
    Item 119's rebuild flagged them; this takes them. All 21 sprintf
    sites in the four `-YY` payload loaders' error paths (atlas, zone
    rules, zone changes, zone links -- each formats the offending
    254-char szLine or a name from it into szErr or szAbb) are
    `sprintf2(S())` now, plus the two compositions in
    DisplayTimezoneChanges() the same warning flagged once the rest
    were quiet. Reachable only from an edited timezone/atlas data file,
    so lower stakes than item 118's five, and the net is accordingly
    lighter: -Wformat-overflow count is zero across the console build
    where it flagged all of them, all three builds compile, suite
    3190/0 with the atlas-sink and ephemeris groups exercising the real
    loaders. Deliberately no corrupt-file fixture: the loaders
    deallocate and replace the process's live timezone tables on entry,
    so a bad-data test would destroy suite state for every later group
    -- item 117's leak class by construction. With this, the B1
    overflow class has no known remaining member.

122. **T2 step (1) taken for the esoteric tables, and the survey found
    its crasher.** The rulership group's machine-checked-encoding
    treatment (item 64) now covers what it skipped: exalt[] all signs
    with none spelled 0, rgObjRay[] all rays 1..7 or 0, every
    rgSignRay[] digit-string made of valid digits, and the derived
    rgSignRay2[] rows each totalling 420 -- the proportion base the ray
    charts divide by. Surveying that last encoding found the bug the
    theme predicts: `-Y7C` range-checks the composed number (1..1234567)
    rather than its digits, so `-Y7C 1 1 8 8 -7` handed EnsureRay() a
    list with no valid ray digit, c stayed 0, and the first esoteric
    chart died on 420/0 -- upstream-inherited, both builds, reachable
    from the command line, a settings file, or Windows' own dialog.
    EnsureRay() now leaves such a row zero, meaning no rays apply,
    which the consumers already handle. Falsified: without the guard
    the new group's regression case dumps core. Suite 3195/0 (+5
    esoteric-tables); Windows build compiles the same fix.

123. **C6 closed: the chart position rings' ownership is written down.**
    CONVENTIONS.md gains "Chart position rings (cp0..cp6)", verified in
    code rather than transcribed from memory: cp0 is the working ring
    and is "the chart" (planet/planetalt/ret/chouse and friends are
    macro aliases into it, refilled wholesale by every CastChart());
    CastRelation() is the one systematic writer of cp1..cp6, casting
    each ring from *rgpci[i] plus its szWheel[i] switches and then
    composing cp0 from the rings per us.nRel; the -r switch handler
    additionally seeds cp1/cp2 from cp0 so -o0 position files can be
    relationship halves; and the time searches use cp1/cp2 as scratch
    and restore only cp0, so those rings are not a relationship's after
    a search. The incident that motivated the finding (relationship
    charts losing their mode on recast, fork-fixed and pinned by
    TestRelationshipModeQt) anchors the section. Documentation only; no
    code change.

124. **T2 step (2): the index domains have names a signature can say.**
    `SIGN`, `OBJ`, `HOUSE`, `ASPECT`, `RAY` are documentation typedefs
    for int in astrolog.h beside the domain constants -- the compiler
    enforces nothing, which is the step the theme chose: the enforced
    version is step (3), explicitly the maintainer's call. Introduced
    the Borrow way: one home, the rule in CONVENTIONS.md ("new and
    touched signatures use them; convert opportunistically, never as a
    sweep"), and eight exemplar signatures converted across three
    domains -- Dignify(OBJ, SIGN), the SzAspect pair, the
    ObjOrbit/ObjMoons/RObjDiam family (ObjOrbit's *return* is an OBJ
    too), and SetObjGlyphNoneCore/SetObjDisp. One mislabeling trap
    written down where it was nearly stepped in: ComputeHouses(int)
    takes a house *system*, not a house index. Net, P7's own:
    a pure documentation change must not move object code, and all 31
    console objects checksum byte-identical before and after; suite
    3195/0; Windows build compiles.

125. **T2 step (3), phase 1: the incident tables enforce their index
    domain.** SIGT and OBJT (astrolog.h, explicit constructors) tag the
    two domains, and the 16 rulership, exaltation and ray tables became
    checked tables (TBLSIG/TBLOBJ/TBLSIGRAY) whose subscript demands
    the tag: `rules[SIGT(i)]` compiles, while `rules[OBJT(i)]`,
    `rules[i]` and `ruler1[SIGT(i)]` are compile errors -- proven with
    a four-way test compile. Item 38's bug class cannot be written
    silently at these tables again. Scope held deliberately at this
    family: full typing of planet[] and friends (775 references, the
    arithmetic and varargs idioms) stays a separate maintainer
    decision, recorded at the theme.
    The churn, measured: 17 files, +264/-200; 170 tagged subscripts (93
    SIGT, 77 OBJT), each placed after reading its loop's domain; eight
    same-domain-generic consumers converted to typed-table pointers
    (RULERSYS, the suite's rgfam, RgRules() and its four users,
    KvHouse, AdjustRulership, NSwRulershipCore, InitColors); the two
    -Y7 registry rows point at .rgn, the one deliberately unchecked
    boundary, where the registry's own lo/hi validation applies.
    Dividend: three sites turned out to index sign tables with a
    *house* -- the natural-sign identification, previously invisible,
    now spelled SIGT(inhouse[i]) with a comment at each site.
    defaults_audit.py learned the checked declaration shape and was
    re-falsified (a shortened ruler2 still trips both legs). Nets:
    suite 3195/0; console and Windows builds; switch matrix 14,378
    lines byte-identical against a HEAD worktree build -- after two
    environmental normalizations worth remembering: a deep worktree
    path truncates the ephemeris path and *changes lookups*, so the
    baseline must build in a short path, and error text spells the
    invocation path absolute-vs-relative; influence matrix identical;
    all six audits clean.

126. **The T2 enforcement campaign is written down for hand-off.** At
    the maintainer's request (switching to a lighter model for the
    mechanical remainder), REFACTORING.md gained "The T2 enforcement
    campaign (step 3) -- state, recipe, ledger": the seven hard rules
    E1 paid for (tables only, tag-is-a-claim, element-variant naming,
    .rgn boundaries, the audit dims map, per-file line endings, one
    family per commit), the nine-step recipe with the exact
    short-path-worktree matrix procedure, and a ledger with two
    pending increments (E2 test-build range asserts, E3 the aspect
    family, ~131 refs measured), one optional (E4), and four families
    closed by written verdict -- including the planet[] core, which
    stays maintainer-gated. Done-when is explicit. Docs only.

127. **T2 increment E2: the checked tables range check themselves under
    the test build.** The compile-time half (item 125) stops a *sign*
    index reaching an object table; it says nothing about an index that
    is the right domain and still off the end -- item 115's shape, a
    star number arriving where an oNorm table was expected. The three
    checked subscripts now carry `AssertIndex(i.n, cSign/oNorm)` inside
    `operator[]`, so that read aborts wherever the suite reaches it.
    astrolog.h only, 23 lines.

    Three things worth keeping:

    - **The assert has to be the C library's.** The codebase's own
      `Assert()` is `#define Assert(f)` unless `DEBUG` (extern.h:419),
      and `DEBUG` is not set by any makefile here -- writing the
      obvious thing would have compiled clean and checked nothing. So
      `<assert.h>` directly, included under `#ifdef QTTEST` beside the
      other conditional headers, with `AssertIndex` defined empty
      otherwise.
    - **It costs the shipped program nothing, and that is provable
      rather than argued.** Every line is inside `#ifdef QTTEST`, which
      only Makefile.qt.test and Makefile.qt.asan define. Baseline built
      from HEAD in a short-path worktree: all 31 console objects
      byte-identical by md5, and `astrolog` itself `cmp`-identical.
      That subsumes the recipe's switch-matrix differential instead of
      approximating it -- an identical binary cannot behave
      differently -- so step 7 now records the shortcut for any future
      release-invisible increment.
    - **Falsified five ways.** The probe was temporarily rewritten to
      subscript past the end and below zero on each of the three table
      types -- `rules[SIGT(cSign+1)]`, `rules[SIGT(-1)]`,
      `ruler1[OBJT(oNorm+1)]`, `ruler1[OBJT(-1)]`,
      `rgSignRay2[SIGT(cSign+1)][1]` -- and each aborted with SIGABRT
      naming its own struct and line, while the in-range control
      returned a value and printed `survived`. A range check that never
      fires is indistinguishable from one that is compiled out, which
      is exactly the trap the first bullet describes.

    Nets: suite 3195/0 with the asserts live, so nothing the suite
    reaches was already indexing out of range; ASAN suite clean too
    (Makefile.qt.asan defines QTTEST, so it gets the asserts for free);
    console, Qt release and Windows builds clean; six audits clean;
    the three generated tables in sync. `CHECKED_TABLE_DIMS` in
    defaults_audit.py needed no entry -- E2 adds no new checked type,
    only behaviour to the three that exist.

128. **T2 increment E3: the aspect family enforces its index domain.**
    `ASPT` and four checked table types (`TBLASP` int, `TBLASPR` real,
    `TBLASPB` byte, `TBLASPK` KI) put the aspect tables behind the same
    door E1 built for the rulership family: `rAspOrb[ASPT(i)]` compiles,
    while `rAspOrb[SIGT(i)]`, `rules[ASPT(i)]`, `rAspOrb[i]` and even
    `ASPT a = i;` are all compile errors -- proven five ways with a
    scratch TU. Eight tables converted: rAspAngle, rAspAngleDef,
    rAspOrb, rAspInf, ignorea, ignoreaMem, kAspA and kAspB.

    Measured: 24 files, +207/-153, 139 tagged subscripts across 19
    files, each placed after reading its loop. Four boundaries took
    `.rgn` -- three switch-registry rows (-YAo, -YAa, -YjA, -YkA, the
    `&rAspAngle[0]` and bare-`kAspA` spellings both) and
    `InitRestrictions()`'s `CopyRgb` pair.

    Four things worth keeping:

    - **The two accessor macros carry the tag, and that is a decision,
      not a shortcut.** `FIgnoreA(a)` and `AdjustAspectCount()` are
      aspect predicates by contract, so tagging inside them keeps the
      arithmetic idiom at the call sites. The price is that rule 2 has
      to be paid at the callers instead: all eight `FIgnoreA()` sites
      were read, and all eight are aspects. `FValidAspect()` turns out
      to be `FBetween(asp, 0, cAspect)` -- exactly E2's assert range --
      so the AstroExpression entry points are checked twice over.
    - **The ledger's cAspect2 trap is real and sits two lines apart
      from its own counterexample.** In `PrintGridCell()` and
      `ChartAspect()` the *same variable* indexes an aspect table and
      then, after a `+= cAspect` or `+ ...*cAspect2`, the larger
      display tables. Tagging by pattern would have been wrong in one
      direction or the other at four sites; reading the two lines
      settles it every time. `szAspectDisp[]` and kin stay plain
      arrays, per the ledger's verdict.
    - **kAspB was added to E3's scope.** The ledger listed seven
      tables; kAspB is kAspA's display-adjusted twin, same domain, same
      dimension, and 15 more subscripts -- 8 of them
      `kAspB[grid->n[i][j]]`, which is item 38's exact shape. Leaving
      the twin untyped beside the typed original is the arrangement
      that confuses the next reader, so it went in the same commit.
    - **The differential had to be rebuilt before it proved
      anything.** Object code is *not* identical here (splitting the
      shared declarator lines moves symbols), so E2's shortcut does not
      apply and the real matrix was needed: switch matrix 14,378 lines
      and influence matrix 3,426 lines, both byte-identical. Those
      cover the switch surface, not rendering, so an aspect-surface
      chart differential was added on top -- 45 text invocations plus 7
      graphics renders, 23,716 lines and 36MB, byte-identical by `cmp`.
      Its **first version was worthless**: a malformed `-qb` made every
      one of the 45 runs print the same parse error, and it "passed"
      identically on both binaries. Checking that the harness is
      sensitive to what it claims to test -- widening an orb, changing
      an aspect colour, changing the aspect count each move the capture
      -- is what caught that, and `diff` on chart output is no good
      either: the escape sequences make it report "binary files
      differ" and print no `<`/`>` lines, so a naive line count reads
      as zero differences. Use `cmp`.

    Nets: suite 3195/0 and ASAN clean, both with E2's range asserts
    live on the new types, so no tag was refuted at run time; console,
    Qt release, Qt test and Windows builds clean; six audits clean;
    three generated tables in sync. defaults_audit.py learned all four
    new types and was re-falsified against rAspOrb -- both the count
    and value legs trip -- and its type alternation now sorts
    longest-first so `TBLASP` cannot shadow `TBLASPR`.

    One loose end, left open deliberately: a single ASan run reported a
    `global-buffer-overflow` and six further runs of the same binary
    would not repeat it, so no trace was captured. It is not on a
    checked table (E2's asserts are live under ASan and would have
    named it), and the fault's shape points at an intercepted libc
    call over a global rather than a subscript. Recorded in
    QT_TESTING.md's ASan section with what is known; not worth looping
    the suite for.

129. **T2 increment E4 closes the campaign, and its first render found
    a live out-of-bounds read.** `RAYT` and four checked types
    (`TBLRAY` int, `TBLRAYK` KI, `TBLRAYV` KV, `TBLRAYSZ` the string
    variant) cover kRayA, kRayB, rgbbmpRay, szRayName and szRayWill;
    iCnstlZodiac becomes a plain `CONST TBLSIG`, no new type needed.
    44 tagged subscripts, 17 files. Enforcement proven six ways, the
    aspect tag included: `rgbbmpRay[ASPT(i)]` is a compile error now
    that two tags exist to confuse.

    **Two extents share one domain, and that is the point.** The colour
    tables run 0..cRay+1 -- the extra slot is an "all Rays" aggregate
    that `InitColors()` fills and the ray ring draws -- while the name
    tables stop at cRay. Each struct asserts its own extent, so handing
    the aggregate index to szRayName is caught rather than read.

    **The bug.** `rgSignRay[]` packs up to three Rays as decimal digits,
    and `-Y7C` range checks the *composed* number (1..1234567), not its
    digits. `EnsureRay()` already knows this and skips a digit outside
    1..cRay (work log item 122 hardened it after an empty list divided
    by zero). `DrawFillWheel()` did not: `rgbbmpRay[n%10]`,
    `[n/10%10]` and `[n/100]` index a 9-slot table straight from user
    data, and `n/100` is not even a single digit -- for the largest
    accepted value it is 12345. Tagging those three sites made the
    claim explicit, and the very first render tested it:

        astrolog -Y7C 1 1 999 -Xv 6 -Xo out.bmp
        Assertion `(i.n) >= 0 && (i.n) <= (7+1)' failed.

    Confirmed pre-existing and not the conversion's doing: the same
    invocation on a HEAD build under ASan reports
    `global-buffer-overflow ... READ of size 8` at xcharts0.cpp:695,
    through DrawWheel/XChartWheel/DrawChartX. Shipped, upstream's, and
    reachable from two documented switches. Fixed in its own commit
    (item 130) so it can be offered upstream.

    **Two traps cost real time getting there**, both worth knowing.
    Reproducing it needed the *right invocation*, and three separate
    things made a wrong one look like "not reachable": `-Xw` takes
    arguments (`<hor> [<ver>]`), so `-Xw -Xo file` eats the output
    switch and the error names some later switch entirely; a console
    binary built at the scratchpad's deep path truncates the ephemeris
    path and changes lookups; and `_X` -- the flag that keeps a console
    run from opening a window -- also turns off the graphics the render
    needs, so it must be dropped when `-Xo` is writing a file. Second:
    building the scratch ASan binary with `make NAME=... CPPFLAGS=...`
    put sanitized objects in the repo root, because the console
    Makefile has no separate object directory. The next ordinary build
    failed to link. `make clean` first, and expect to clean after.

    **lonCnstlZodiac stays plain, by verdict.** One subscript,
    dimension cSign+2 where the +1 is a real thirteenth boundary, so it
    would need a bespoke type for a single site. Measured and recorded
    rather than converted -- which the ledger allows and which is the
    honest answer here.

    Nets: suite 3195/0 with the asserts live; console, Qt release, Qt
    test and Windows builds clean; switch matrix 14,378 lines and
    influence matrix 3,426 identical; a new ray-surface chart
    differential -- 19 charts, 10,442 lines, 5 renders including all
    four `-Xv` ray fills -- identical by `cmp`, and sensitivity-checked
    against a ray colour and a sign Ray change first. Six audits clean;
    defaults_audit.py learned the four new types and was re-falsified
    on kRayA, both legs. **T2's done-when is met: every table family
    that has ever shipped an incident is behind a tag.**

130. **The Ray wheel fill reads a digit that names no Ray.** The bug
    E4's tag exposed, fixed in its own commit so it can be offered
    upstream -- it is upstream's, in shared core, with no `QT` guard
    anywhere near it.

    `rgSignRay[]` packs a sign's Rays as decimal digits, and `-Y7C`
    range checks the composed number (1..1234567) rather than each
    digit. `EnsureRay()` has known that since item 122 and skips a
    digit outside 1..cRay. `DrawFillWheel()` did not: it indexed the
    nine-slot `rgbbmpRay[]` with `n%10`, `n/10%10` and `n/100` straight
    from that user data -- and the third is not a digit at all, being
    12345 for the largest accepted value. `KvRayDigit()` now applies
    EnsureRay's rule at the read, mapping a digit that names no Ray to
    the zero this code already uses for "absent".

    **What actually changes, measured rather than asserted.** For every
    valid Ray list nothing moves: the switch matrix and the ray-surface
    chart differential are byte-identical across the fix. Only lists
    containing the digit 8 render differently, and that is the point --
    8 is not a Ray, but slot 8 is the "all Rays" aggregate colour, so
    the old code silently painted a sign with it. Now it reads as
    absent, which is what EnsureRay() already believed. The genuinely
    out-of-range lists (999 and kin) happen to render the same as
    before, because whatever followed the array read as zero: **the
    symptom was invisible, which is how this shipped for years. The
    defect was the read.**

    The regression test (`ray-digit-fill`, 7 assertions) renders the
    wheel with an empty list, a valid one, three all-invalid ones and
    the maximum, and pins each against the empty or two-Ray render. Two
    things about it are deliberate. It has a leg asserting that a
    *valid* list renders differently from an empty one -- without that
    the whole test would pass on a build that had stopped filling by
    Ray entirely. And its first version was wrong in a way worth
    keeping: it assumed 1234567 was an all-invalid list, when its low
    digits are Rays 7 and 6, so the test failed and the *code* was
    right. Falsified by reintroducing the bug, which aborts on E2's
    range assert -- a regression here is a SIGABRT, not a FAIL line,
    and the test says so in a comment.

131. **The third real-data session, and the divergence that had
    quietly reverted.** Item 7's practice again, with the maintainer's
    config and chart files. Two ASan sweeps with E2's range asserts
    compiled into a console build came back clean -- 625 text-chart
    runs (25 modes x 25 real charts, 600 producing charts) and 275
    graphics runs, every one writing a bitmap, zero sanitizer or assert
    hits. That included `-Xv 1`..`-Xv 7`, so the Ray wheel fill ran
    against real composed Ray lists: item 130's code on real data.

    **The finding was in the GUI, not the calculation.** Setting /
    Display Settings' "Number of Aspects to Include" could be *lowered
    but never raised* -- a one-way ratchet that silently did nothing.
    `DlgDisplay`'s store kept the loop that restricts aspects above the
    new count but had lost the one that un-restricts below it, so
    `AdjustAspectCount()` recomputed the count straight back down from
    the restrictions. Restored, with the ordering that makes it work
    (both loops before `us.nAsp = na`) spelled out at the site.

    Three things worth keeping:

    - **Not a regression from this week.** `git log -L` on those four
      lines says commit `3c6c55a` added the dialog with the loop
      correct -- it saved the old count in `naOld` -- and `bf92b9e`, a
      transcription pass, deleted `naOld` and the loop together. The E3
      commit touched the surviving line only to add its domain tag.
      Checking that before writing anything is the difference between a
      fix and a fix plus a wrong story.
    - **The plan claimed the opposite, in writing.** Section 8.12
      recorded this as a deliberate divergence where "Qt saves the old
      value first, so it works", flagged so it would read as a choice.
      It had been false since `bf92b9e`. The lesson is narrow and
      general at once: **a divergence documented but not tested is a
      divergence waiting to revert.** The new `aspect-count` group is
      the guard, falsified two ways -- deleting the loop fails all five
      assertions, and reproducing Windows' ordering (`us.nAsp = na`
      before the loop) fails all five as well, which is what proves it
      guards this divergence and not merely "some loop".
    - **The other route was fine, and checking that mattered.** Ticking
      an aspect's box in Aspect Settings does un-restrict, confirmed
      live by reading the count back afterwards. So the defect narrowed
      to one control rather than to aspect restriction generally.

    Method notes, all of which cost time: `qtdrive.sh run` takes
    `--args` exactly as `tree` does, and without it the app comes up on
    the *repo's* astrolog.as, so a real-config test quietly measures
    the wrong program -- the tell was a field reading 5 where the
    config says 9. `expect-value` on a check box returns its label, not
    its state, so a checkbox assertion has to go through an observable
    the program computes; here that was the aspect count itself. Set
    Colors is in the **View** menu, not Setting. And piping a long
    sweep into `head` kills it by SIGPIPE partway: the first graphics
    sweep "passed" over 11 of 25 files that way.

    Nets: suite 3207/0; console, Qt release, Qt test and Windows builds
    clean; six audits clean.

132. **The divergence list gets tests, and two of its five rows turned
    out to be wrong.** Item 131 found a documented divergence that had
    silently reverted; this is the pass that stops the class. The rule
    now at the head of "Known divergences from Windows": every
    behavioural divergence names the test that holds it, falsified
    *against the Windows behaviour specifically*.

    Why the class exists at all is structural and worth stating once.
    The four `rc_*_audit.py` scripts all compare this port **against**
    `astrolog.rc`, so anywhere it intends to differ from Windows is
    outside what they can see by construction. Divergences were the one
    body of behavioural claims with no mechanical check of any kind.

    Testing the five table rows resolved three without a decision being
    needed:

    - **Display Settings' aspect count** had reverted (item 131). Now
      fixed in *both* builds -- `wdialog.cpp` too, at the maintainer's
      direction, so upstream can take it -- which means it stopped being
      a divergence and moved to "Features this fork adds to both
      builds".
    - **Atlas City Coloring** claimed the port writes `gs.fLabelCity`
      where Windows writes `gs.fLabelAsp`, calling that an upstream
      typo. The note was wrong, not the code: `-H` documents
      `-XL[1-5]` as setting "how to color cities (when -XA is on)", and
      `xcharts0.cpp:2088` and `xcharts1.cpp:174` both read `fLabelAsp`
      for exactly that; `-XL` is whether cities appear at all. Taking
      the row's advice fails two assertions of the new group. Row
      deleted with the measurement.
    - **The restriction checkbox sense** matches Windows and always
      did since 8.9, so it is a historical note about older Qt builds
      rather than a live divergence, and `dialog-buttons` already pins
      the sense.

    Two rows keep their behaviour and gained guards -- Daylight
    Autodetect surviving the chart info dialog, and the IBM
    line-drawing adjustment not being copied. One, the command line
    dialog's `us.fLoop`/`is.fMult` save-restore, is recorded as
    **untested**, because its only observable is a typed line that
    starts a multi-chart run and the in-process suite cannot drive
    that. Saying so beats letting it look covered.

    **The line-drawing test took three drafts, and each failure was the
    same shape.** It first counted CP437 rule bytes in a rendered aspect
    grid. That passed alone and failed in the full run, because
    `TestAllMenuActionsQt()` fires 338 items and leaves `us.fAnsiChar`,
    the unrestricted object set and `us.nCharset` wherever they land --
    and the grid's size is the object count while `-Ya` encodes the same
    rules as one byte or three. Pinning the charset to `ccIBM` then made
    it fail *alone* as well. The third draft stopped hunting for
    characters and rendered the same grid twice, with `gs.nFontTxt` off
    and on, demanding the bytes match: that is the actual claim, and it
    is immune to every one of those inherited settings. Widening the
    `#ifdef WIN` to QT makes 3,930 bytes differ.

    Also: the first version of this group ran the grid render inside the
    dialog tests and aborted the suite several groups later with
    "invalid stdio handle" -- calling `Action()` needs
    `TestLongStringsQt()`'s whole prologue (no-popup, `Borrow` on
    graphics/progress/relationship) and its epilogue (chart state put
    back, `CastChart(1)`). It is its own group beside that test for the
    same reason.

    Nets: suite 3213/0; console, Qt release, Qt test and Windows builds
    clean; six audits clean. One run in the middle of this work aborted
    with glibc's "buffer overflow detected" and then passed twice --
    the same intermittent fault ASan reported during item 129, now
    seen by a second detector, which narrows it to a formatting or copy
    overrun rather than a subscript. Still open, recorded in
    QT_TESTING.md's ASan section; not chased, at the maintainer's
    direction.

133. **Hunting the intermittent found two other bugs, deterministically.**
    Asked to try for the intermittent overflow again, the useful move was
    not more suite runs -- 17 under gdb found nothing -- but pointing a
    sanitizer at a harness that is already exhaustive. **`tools/switch-matrix.sh`
    under a console ASan build reported two, on its first run:**

        make clean
        make NAME=/tmp/ha CPPFLAGS="-DQTTEST -fsanitize=address -g -O0 \
          -Wno-write-strings -Wno-narrowing -Wno-comment" \
          LIBS="-fsanitize=address -lm -lX11 -ldl" -j4
        make clean && make -j4          # the objects are shared; see the traps
        ASAN_OPTIONS=detect_leaks=0 tools/switch-matrix.sh /tmp/ha

    Both are upstream's, and both are now fixed:

    - **`ChartSector()` read one past `rgc[]` and `pluszone[]`**
      (charts1.cpp). `GFromO(o)` is `(rDegMax - o)/10`, so a position of
      exactly 0 gives 36.0 and `sec = 37` -- one past tables that hold
      `cSector+1` entries indexed 0..36. The graphics twin already knew:
      `xcharts0.cpp`'s gSector branch carries the same arithmetic with a
      wrap and a comment explaining it. The text path never got it, so
      it also printed sector 37, and 19 and 13 in the 18- and 12-sector
      columns, where all three should read 1. Reached by any object
      still sitting at 0.0, not only one truly at 0 Aries.
    - **`-YXDD` freed or overwrote a compiled-in glyph** (switch.cpp).
      It copied through `FCloneSz()`, which is `fDestConst = fFalse` --
      "the destination is heap owned", so reuse its buffer if the source
      fits and free it otherwise. A glyph still at its default is
      neither. The sibling branch handling `-YXD`/`-YXD1` has always
      passed the guard; this one never did, going back to the original
      `xscreen.cpp` case the M4/M5 migration moved verbatim.
      **`astrolog -YXDD 5 6` segfaulted the release build** -- the plain
      switch matrix prints "Segmentation fault (core dumped)" at HEAD
      and a settings dump after, which is the whole behavioural diff for
      this increment.

    **No unit test for the glyph one, deliberately.** A first attempt
    passed with the bug reintroduced, and an indiscriminate test is
    worse than none, so it was deleted rather than committed. The
    standing net is the matrix itself: the invocation is already in it,
    and at HEAD it crashes. The ASan recipe above belongs in the
    pre-release checks, not the pre-commit ones -- one run, two real
    bugs, in a harness that had been run dozens of times without one.

    Not the intermittent, though: that one ASan called a *global*
    buffer overflow and this pair are stack and heap. Still open.

    Two method notes. The matrix pipes each run's stderr through
    `head -2`, so an ASan report arrives decapitated -- it names the
    invocation and nothing else, and the trace has to be got by
    re-running that one invocation directly. And running the Qt suite
    4-way parallel to hunt faster does not work: `long-strings` and the
    new `line-drawing` both write a fixed filename under `TMPDIR`, so
    concurrent runs clobber each other and fail 3-9 assertions that have
    nothing wrong with them.

    Nets: suite 3213/0; console, Qt release, Qt test and Windows builds
    clean; six audits clean; the matrix ASan-clean where it reported two
    findings before, and its only behavioural diff the crash that stopped
    happening.

134. **The graphics surface under ASan: 254 renders, five more
    out-of-bounds bugs.** Item 133 sanitized the *switch* matrix, which
    drives the -X family but sends stdout to /dev/null -- so it covers
    the parse and almost none of the drawing. This sweep renders: every
    graphics chart mode bare, every graphics option on a wheel and on a
    globe and a world map, and each of the other output writers, all to
    a file under a console ASan build. 254 invocations, 12 reports,
    five distinct defects, all upstream's and all fixed. It went from
    12 hits to 0 in four rounds, each round's fix uncovering the next.

    **Three are one mistake made three times: a bounds guard that is
    present but sequenced after the access it guards.**

    - `FProper()` (xcharts2.cpp:81), `EnumMoonsRing()` twice (302, 323):
      each wrote `ignore[objP] || !FHasMoon(objP)`. `ObjOrbit()` answers
      **-1** for a body that orbits nothing and `FHasMoon()` *is* the
      `>= 0` test -- so putting it second reads `ignore[-1]`, one byte
      below the array, which ASan names exactly. Reordering is the whole
      fix. `-X8` on a wheel was enough to reach all three.
    - `WriteXBitmap()` (xdevice.cpp): `BmGetXY(x+i, y)` was fetched
      before the `(x + i < gs.xWin)` test in the same expression -- `^`
      binds tighter than `&&`, so the pixel was read and only then
      discarded, running off the end of the final column group of every
      row. Heap overread on every XBM write.

    These three are the shape T2's theme predicted and item 37 first
    recorded: a "none" value of -1 used as a subscript. They are outside
    T2's enforcement surface because `ignore[]` is the object-domain
    core, the row that stays maintainer-gated.

    **The fifth is a long string into a fixed buffer**, item 114's class
    again: `WriteXBitmap()` did `sprintf(szT, "%s", szName)` with
    `szT[cchSzDef]` and `szName` the whole output path -- a 95-byte write
    into 80. It now finds the path's last component in `szName` itself
    and copies only that, bounded. **This crashes the release build**:
    `-Xbn`, `-Xbc` and `-Xbv` abort outright for any output path over
    about 79 characters, which a deep directory reaches easily.

    Behaviour is unchanged everywhere it did not previously crash. All
    three `ignore[-1]` sites already continued past the object either
    way -- only the read was illegal -- and the `BmGetXY` reorder yields
    the identical bit, since out of bounds scored 0 before and after. A
    12-case render differential against HEAD is byte-identical on the
    seven cases HEAD survives, and the switch matrix is identical.

    **Not the intermittent.** That one ASan called a *global* buffer
    overflow; of these five, three are global but need `-X8`, which the
    suite does not set, and nothing in qttest.cpp reaches
    `WriteXBitmap()` at all. Checked rather than assumed, because a
    fortify abort on a long path was a tempting match for the observed
    symptom.

    Nets: suite 3213/0; console, Qt release, Qt test and Windows builds
    clean; six audits clean; graphics sweep 254/0 where it reported 12.

135. **The object core gets a range guard, and the gated question
    turns out to have been the wrong one.** Taken at the maintainer's
    direction on the one row T2 left for their decision. Measuring it
    first changed what the increment should be.

    **A domain tag would not have caught the incidents that motivated
    this.** All three of item 134's bugs were `ignore[-1]`, and
    `ignore[OBJT(-1)]` compiles perfectly well: a tag is a claim about
    which *domain* an index came from, and says nothing about its
    *range*. What catches -1 is the range assert -- and that turns out
    to be **separable from tagging**, which is the whole finding here.

    `GRDOBJB` takes a plain `int`, asserts `0..cObj` under `QTTEST`,
    and is otherwise an array. So converting `ignore` and `ignore2`
    changed **none of their 338 subscript sites** -- the entire cost was
    41 raw-storage boundaries taking `.rgn` (CopyRgb/sizeof pairs, two
    registry rows, the restriction dialogs' by-pointer array selection).
    Compare E1: 170 subscripts by hand for the tagged equivalent.

    **Proven by falsification, and the proof is the point.** Reverting
    item 134's `FProper()` fix makes a plain `-DQTTEST` build abort on
    `-X8` with `GRDOBJB::operator[]: Assertion (i) >= 0 && (i) <=
    (cObj) failed` -- no sanitizer, no ASan sweep, no luck required.
    That is the mechanism that would have caught all three of item
    134's bugs the moment any test reached `-X8`.

    Nets: suite 3213/0; console, Qt release, Qt test and Windows builds
    clean; switch and influence matrices identical; a 10-case render
    differential identical; six audits clean after defaults_audit.py
    learned the `GRD*` declaration shape, re-falsified on a shortened
    `ignore`.

    **What this leaves.** O2 -- planet[]/chouse[] and the rest of the
    objMax family -- is the same mechanism applied mechanically, and is
    now a small job rather than the multi-session one item 125 measured,
    because the 1,000-odd subscripts do not move. The only real work is
    that `planet`/`chouse` are `cp0.obj`/`cp0.cusp`, members of `CP`, so
    the guard lives in the struct and the 16 `cp*.obj`-as-pointer sites
    take `.rgn`. **Domain tagging the object core is no longer the
    gated question**: it is closed pending evidence, and the evidence it
    waits for is a *cross-domain* incident here, of which there has
    never been one -- every incident in this family has been a bad
    value, not a wrong domain.

136. **O2: the object core's guard reaches the whole chart, and found
    a bug on its first run.** `GRDOBJR`, `GRDOBJI` and `GRDSIGR` over
    `CP`'s ten arrays -- obj, alt, dir, diralt, dirlen, dist, cusp,
    cusp3, house -- plus `force` and `kObjA`. That is roughly a thousand
    subscripts, `planet[]`/`chouse[]`/`ret[]` and the rest of the
    aliases included, and **not one of them changed.**

    The entire cost was eleven boundary sites and three signatures. The
    signatures are the interesting part: `GetAspect()`, `GetParallel()`
    and `GetDistance()` took `CONST real *`, so every caller decayed a
    guarded table into a bare pointer. Taking `CONST GRDOBJR &` instead
    means no call site changes *and* the guard follows into the callee
    -- E1's rule 1 exemplar, applied where it saves work rather than
    costs it. `DrawSymbolRing()` likewise.

    `cusp`/`cusp3` are the sign domain inside an object-domain struct
    and needed their own bound, which is the one thing here that reading
    the struct rather than the aliases makes obvious.

    **It caught a real bug immediately.** The first suite run after the
    conversion aborted:

        astrolog-qt-test: astrolog.h:1706:
          real& GRDOBJR::operator[](int):
          Assertion `(i) >= 0 && (i) <= (cObj)' failed.    // i = 134

    `XChartAstroGraph()` (xcharts1.cpp) walks `for (i = 0; i <=
    is.nObj*2+1; i++)` with `j = i >> 1` as the actual object -- the
    loop is doubled because each object gets two labels. Two lines used
    `ret[i]` where every other object read in the loop, including the
    `DrawColor()` immediately above each, uses `ret[j]`. So it read past
    `cp0.dir` whenever `is.nObj*2+1` exceeded cObj, and used the wrong
    object's direction for the dash pattern even when it didn't.
    Upstream's, and on a path the suite has been firing every run.

    No render moves: a 12-case differential including `-L` and `-L0` is
    byte-identical, and the switch matrix is identical. Pure UB removal.

    **A candidate for the intermittent, stated as a candidate.** `ret`
    is `cp0.dir` and `cp0` is a global, so an overread there is a
    *global* buffer overflow, which is what ASan called the intermittent
    (item 129) -- and it is intermittent for the right reason, since
    whether `is.nObj*2+1` passes cObj depends on which objects the 338
    menu items left unrestricted. What does not fit is the glibc
    "buffer overflow detected" sighting (item 132), which only
    instruments the `__*_chk` family and so cannot be a plain array
    read. Two overlapping faults would explain both. Not claimed
    closed.

    Nets: suite 3213/0; console, Qt release, Qt test and Windows builds
    clean; switch matrix and a 12-case render differential identical;
    six audits clean.

137. **O3 closes the object core's range guard.** `rgobjset`,
    `szObjDisp`, `kObjB`, `rgobjList` and `rgobjList2` -- about 490 more
    subscripts, and again not one of them changed. Three new element
    variants (`GRDOBJK` for KI, `GRDOBJSZ` for the display names,
    `GRDOBJSET` for the settings rows) and four boundary fixes.

    `GRDOBJSET` is the one worth knowing about: the per-object settings
    table stops at `oNorm1`, the collective fixed-star row, where every
    other object array runs to `cObj`. Reading it with a plain object
    number past that is exactly work log item 115's bug, which is why
    `RObjInf()`/`RTransitInf()` clamp with `Min(i, oNorm1)`. The guard
    now carries that smaller extent in the type, so a site that forgets
    the macro is caught rather than reading a neighbouring global.

    Its initializer needed one extra brace level, unlike every earlier
    conversion: brace elision covers a flat list of scalars but not a
    list of braced structs.

    **ignoreMem and ignore2Mem stay plain, by verdict.** They are never
    subscripted anywhere -- only `CopyRgb`'d wholesale -- so a guard
    would add `.rgn` at every use and check nothing.

    Nothing new found this time: suite 3213/0 first run. The dividend
    was O2's.

    defaults_audit.py learned all seven `GRD*` shapes plus the guarded
    `rgobjset` (which it locates differently, having no bracketed
    dimension to read). Falsifying the count leg exposed a weakness in
    the audit itself, now fixed: a short `rgobjset` reported the row
    count and *then* crashed the value leg on the short list, burying
    its own finding under a traceback. It now reports both and stops.

    Nets: suite 3213/0; console, Qt release, Qt test and Windows builds
    clean; switch and influence matrices identical; a 10-case render
    differential identical; six audits clean, re-falsified both legs.

138. **The sanitizer sweeps become a tool.** Items 133 and 134 found
    seven out-of-bounds bugs between them -- one crashing the release
    build -- using scratch scripts that were then deleted, leaving a
    paragraph in QT_TESTING.md describing what to rebuild. That is
    exactly the failure the divergence list had (item 132): **prose
    describing a check is not a check.** `tools/asan-sweep.sh` is both
    halves, `switches` or `graphics` or neither for both, exiting
    non-zero on anything reported.

    It builds its own console binary with `-fsanitize=address` and
    `-DQTTEST`, so the checked tables' range guards are live alongside
    ASan -- which matters, since the guards catch the `-1` subscripts
    ASan sees only as a one-byte underread.

    Four arrangements in it are load-bearing and the header says so:
    `make clean` on both sides of the overridden build, because the
    plain Makefile shares the repo's object directory; a **short** path
    for the binary, because a deep one truncates the ephemeris path and
    changes lookups; a deliberately **long** path for the output file,
    because an 80-byte buffer took the whole output path in
    `WriteXBitmap()` and a short scratch directory would have hidden it;
    and re-running any invocation the switch matrix reports, because
    that harness pipes each run's stderr through `head -2` and a
    sanitizer report arrives decapitated.

    **Falsified in both halves and in both directions.** Reverting
    item 134's `FProper()` fix makes the graphics half report `-X8`,
    `-XG -X8` and `-XW -X8` and exit 1; reverting item 133's `-YXDD`
    fix makes the switch half name `-YXDD 5 6`, recovered from the
    decapitated report. Restoring each returns exit 0.

    Writing the falsification found a defect in the tool itself: the
    switch half appended its hit list to a file it never truncated, so
    a **clean tree reported the previous run's hits**. A check tool that
    cries wolf is worse than none, and it was only visible by running
    clean *after* dirty rather than the other way round -- worth
    remembering as the order to falsify a stateful check in.

139. **The switch matrix was comparing 19% of what it selected.**
    `tools/switch-matrix.sh` is the gate every parser increment in this
    project was proven against -- M1-M10, P1-P4, all "byte-identical
    over 14,378 lines". It prints each run's stderr plus the settings
    the run saves, filtered by a long grep, and capped the filtered set
    at `head -30`. **That filter selects 159 lines out of a settings
    dump, and dumps for the macro-defining invocations run past 200.**
    So the gate had been diffing the first thirtieth of the surface it
    had already chosen, and any behaviour past that line was invisible
    to it -- across 529 invocations, for the whole T3 and phase-2
    campaigns.

    Found by accident, and only because the change that found it *added*
    output: five new settings lines pushed five others off the bottom of
    the window, and the diff showed five plausible-looking deletions
    that were nothing of the kind. A fixed window in a harness whose
    whole contract is "empty diff = proven" is a way to be told a
    change is safe by a check that never looked at it.

    The cap is 1000 now, above any dump (a whole settings file is ~330
    lines). The artifact grows from 14,407 lines to 75,471, which is the
    real size of the surface it was always supposed to cover.

140. **`-bm` and `-bp` cast a chart of nothing, and said nothing.**

        $ ./astrolog -bm -qa 6 15 1990 12:00 0 122W19 47N36 -v
        Sun :  0Ari00   + 0:00'   [11th house]  +0.000
        Moon:  0Ari00   + 0:00'   [11th house]  +12.20
        ...                       every body, no error

    `NSwb()` (switch.cpp) ends every backend suffix with an
    unconditional `SwitchF(us.fEphemFiles)`. Two suffixes sit behind
    `-0` guards -- `'p'`/`'m'` behind `us.fNoPlacalc`, `'J'` behind
    `us.fNoNetwork` -- and when a guard refused, the assignment was
    skipped but the toggle at the bottom still ran. The shipped
    `astrolog.as` sets `=0b`, so asking for the Matrix or Placalc engine
    turned the *working* engine off and put nothing in its place:
    `FCmMatrix()` is `(!us.fEphemFiles && us.fMatrixPla)` and neither
    half held. Every position stayed at the zeroed `cp0`.

    Upstream's, in shared core, no `QT` anywhere near it -- the Windows
    build does the same. Reachable from the command line with the
    shipped settings file, and from any settings file carrying `=bm`.

    **Fixed by refusing the switch** the way `us.fNoGraphics` and
    `us.fNoRead` already do in the same file -- `ErrorArgv("bm")` and
    `tcError`. Narrowly: only a request to turn the backend *on* is
    refused, because the settings writer now emits `_bp`/`_bm` into
    every saved file and those have to stay loadable under `=0b`.
    `tools/settings-round-trip.sh` leg 2 caught that within a minute of
    the first attempt, which is what that leg is for; `bp`/`bm`/`bJ`
    join its exempt list for the same reason `0b`/`0n` are already on
    it -- they are gated *by* the one-way family.

    **And the settings writer dropped the whole backend.**
    `FOutputSettings()` wrote none of `nSwissEph`, `fPlacalcPla`,
    `fMatrixPla`, `fPlacalcAst`, `fMatrixStar` -- grep io.cpp for any of
    the five and it was empty. Choosing Moshier or JPL in Calculation
    Settings, saving, and reloading gave you Swiss back, silently. This
    is the class plan item 8 declared closed on 2026-08-26; the `-b`
    sub-family was missed, and `registry_audit.py` cannot see it because
    it checks that every *written* spelling resolves to a row, not that
    every setting gets written. Five lines now, placed ahead of the
    `=b` line (which must settle `fEphemFiles` last, since every suffix
    toggles it) and ahead of `=0b`/`=0n` (so a saved file applies its
    backend before locking the old engines out). One forced line carries
    `nSwissEph`, because its four spellings are mutually exclusive
    toggles that each clear the field.

    C4 surveyed this encoding on 2026-08-29 and documented the state
    table at the field declarations. Nobody checked whether it was
    *persisted*. The comment it left behind -- "the settings writer
    emits forced =/_ prefixes" -- was not true when it was written.

    **Net:** the 529-invocation matrix, at its new full window, diffs to
    exactly two things and nothing else -- the five added lines in every
    dump, and the two invocations (`=bp`, `=bm`) that now refuse and
    write no dump at all. Suite 3520/0, six audits clean, all three
    generated tables in sync, round trip clean on all three legs, and
    the console, Qt release, Qt test and Windows builds all compile.

141. **The numeric oracle: the suite gets its first opinion about
    whether the numbers are right.** Asked what would move the codebase
    forward, and the answer was in what every net here has in common.
    `tools/switch-matrix.sh` byte-diffs the tree against an older build
    of *itself*; `tools/win-tests.sh` and the text-chart diff compare
    two builds that share this core; `tools/asan-sweep.sh` proves no bad
    memory access. All of them can prove **unchanged**. None can prove
    **correct** -- and a differential actively locks a wrong answer in,
    since fixing a defect that shipped in 1993 reads as a regression.
    Before this, 3213 assertions across 44 groups contained **two**
    about a computed number, both house cusps on the Matrix path.

    That is a strange gap for a program whose entire purpose is
    producing those numbers, with **four** planetary engines and **40**
    house systems reading them.

    `TestNumericOracleQt()` asks the ephemeris library the same question
    Astrolog asks it and requires the same answer. The
    Astrolog-object-to-Swiss-body mapping is written out in qttest.cpp
    rather than read from calc.cpp, so it is an independent
    transcription. Four legs, 307 assertions, 37ms:

    - **Swiss glue.** 15 bodies x 7 epochs 1900-2080 against
      `swe_calc_ut()`. Measured agreement is **exact** -- 0.000000
      arcsec, every body, every epoch. The 1e-9 tolerance is slack
      against compiler reassociation, not a fudge factor.
    - **Sidereal.** `is.rSid` is added in `ProcessPlanet()` while
      `SEFLG_SIDEREAL` subtracts the ayanamsa inside the library, which
      reads like a double application. It measured exact. Pinned.
    - **Matrix cross-check.** The same charts on Astrolog's own
      formulas, per-body tolerances at about 2x the measured worst case
      (Sun-Mars 0.010 deg, Jupiter-Neptune 0.255, Pluto 0.867,
      Chiron/Ceres/Pallas 2.33, Juno 8.17, Vesta 11.01). Two independent
      implementations of the solar system, checked against each other.
    - **House partition.** All 40 systems: twelve positive gaps summing
      to 360. This is the `SwissHouse()` surface whose own comment says
      "largely copied from swe_houses()".

    **Falsified in all four legs**, which is the only reason to believe
    any of it. Perturbing `ComputeEphem`'s `planet[i]` by 1e-6 degrees
    -- 0.0036 arcsec -- fails 120 assertions. Forcing `FCmMatrix()`
    false, which reproduces item 140's failure exactly, fails 104.
    Swapping two cusps in `SwissHouse()` fails 20. (A first attempt
    perturbed `ProcessPlanet()` and changed nothing, because the Swiss
    path sets `planet[i]` directly and never calls it -- a reminder that
    a falsification can fail by aiming at the wrong line.)

    **It found item 140 within twenty minutes**, before a line of it was
    written, by the simple act of asking two engines the same question.

    **The group passed alone and failed 222 assertions in the full run**
    -- the inherited-state trap this file's own header warns about.
    `CastChart()` rewrites every position *again* after `ComputeEphem()`:
    harmonic, decan, dwad and navamsa each map `planet[]` through a
    function of itself, and `TestAllMenuActionsQt()` leaves all four set.
    Diagnosed by dumping globals solo and in the full run and diffing
    them (work log item 57's method) rather than guessing one variable
    per rebuild: 25 borrowed settings were identical and `us.fStar`
    plus `is.nObj` were not, which was a red herring; instrumenting
    `ComputeEphem` showed it producing the *correct* Sun and something
    later overwriting it.

    Two harness fixes travel with it. `Check()` formatted its own
    message with unbounded `vsprintf` into a fixed buffer -- the same
    class this project keeps finding, in the check harness itself; it is
    `vsnprintf` now. And the offscreen QPA plugin's
    "does not support propagateSizeHints()" warning, which has no
    logging category and so cannot be filtered by `QT_LOGGING_RULES`,
    is dropped by a `qInstallMessageHandler` under `QTTEST` that passes
    every other Qt warning through.

    **Suite: 3520 passed, 0 failed.**

    Two things this measured and did *not* fix, both recorded rather
    than acted on. **Topocentric houses degenerate beyond the polar
    circle**: at 78N the twelve cusps run backwards and wrap the circle
    three times, so house assignment is meaningless. Astrolog guards
    Placidus and Koch at extreme latitude (`ComputeHouses()`,
    calc.cpp:508, falls back to Porphyry with a warning) and Swiss
    substitutes Porphyry for its own Placidus/Koch -- but nothing covers
    Topocentric, which is the same pole construction, and Longyearbyen
    at 78.2N is in the shipped atlas. Pullen (S.Delta) produces
    zero-width houses at 70N and above. Both are shared core and affect
    the Windows build; changing house math is a maintainer decision, so
    the oracle asserts the partition invariant at mid latitude only and
    this note carries the measurement. **And an unexplained intermittent
    in `long-strings`**: two failures in about sixteen runs on
    2026-08-31 ("mode 27", then "mode 11", both "0 bytes"), then zero in
    thirty-two consecutive runs after, with and without these changes.
    Not reproduced, not diagnosed, not claimed fixed.

142. **The intermittent, caught: `acos` out of domain for two objects at
    the same place.** Items 133 and 3a6dd79 hunted this twice -- 17 runs
    under gdb, an ASan sweep of the whole switch matrix -- and left it
    open, described as "a *global* buffer overflow" that would not
    reproduce. It reproduces about one full suite run in six, and the
    reason it kept escaping is that **the sanitizer build cannot see it**:
    `Makefile.qt.asan` compiles at `-O0`, where `_FORTIFY_SOURCE` is
    inactive, and this is a fortify detection. It has to be hunted in an
    optimized build with `-g`, which is a one-line change to
    `Makefile.qt.test`.

        #13 ___sprintf_chk (s=<SzDegree(double)::szPos> "-2147483648:-21",
                            slen=15, format="%3d%c%02d'")
        #15 SzDegree (deg=nan(0x8000000000000)) at general.cpp:1870
        #16 PrintMidpointSummary (count=5995, rSpanSum=nan) charts1.cpp:1239
        #17 ChartMidpointCore (fRel=0) at charts1.cpp:1353

    **Root cause, `SphDistance()` (general.cpp:574).** It hands `acos`
    the spherical law of cosines,
    `sin(lat1)sin(lat2) + cos(lat1)cos(lat2)cos(dLon)`. For two points at
    the *same* position that expression is `sin^2 + cos^2`: exactly 1.0 in
    arithmetic, and **above** 1.0 in double precision for 3.75% of
    latitudes -- 9,636 of 256,858 in a sweep, worst excess 2.22e-16.
    `acos` of anything past 1.0 is NaN. Two objects sharing a position is
    not exotic: a tight conjunction does it, and so do two slots both
    still sitting at 0.0.

    The NaN then propagates. `ChartMidpoint()` adds it into `rSpanSum`,
    `PrintMidpointSummary()` divides and calls `SzDegree()`, `(int)NaN` is
    `INT_MIN` on x86, and `"%3d"` -- a *minimum* width, not a maximum --
    writes `-2147483648:-2147483648'` into `static char szPos[15]`.
    Fortify kills the process. Intermittent because whether any pair
    coincides depends on the chart, and the group that tripped it
    (`long-strings`) casts from the current moment.

    **The codebase already knew.** `xdevice.cpp:829` guards its own two
    `RAcosD` arguments with exactly this clamp and the comment "Roundoff
    may put it slightly outside Acos range." `SphDistance()` never got
    it. Fixed the same way, plus both `SzDegree()` buffers moved to the
    bounded `sprintf2(S(...))`/`SO(...)` idiom this codebase already
    has -- theme T5 -- so no input can overflow them again regardless.

    **Falsified deterministically**, which an intermittent otherwise
    resists: the oracle's leg 5 sweeps 256,858 latitudes and asserts no
    NaN; reverting the clamp fails it with exactly 9,636, matching the
    standalone measurement. Twelve consecutive full-suite runs clean
    after the fix, against roughly one abort in six before.

    Two notes. The residual distance for coincident points is ~1.7e-06
    degrees rather than 0, which is the law of cosines' precision floor
    (`acos(1-eps) ~ sqrt(2*eps)`) and not a defect; haversine would fix
    it and change every distance the program prints, so it is not on the
    table, and the test's bound says so. And `RAcos` sites at
    calc.cpp:336 (semi-diurnal arc, `-tan(lat)tan(decl)` exceeds 1 in the
    circumpolar case) and matrix.cpp:358 have the same shape and no
    guard; neither has produced an incident, and neither was touched.

    Both fixes are shared core with no `QT` anywhere near them, so the
    Windows build gets them too.

143. **T5 retired where it can be: 1,055 unbounded `sprintf` calls become
    bounded, and the gate that could not see them gets written.** Item 142
    was this theme drawing blood -- an intermittent that killed the process
    and survived two hunts, and the mechanism was `sprintf` into a 15-byte
    buffer. Fixing that one buffer by hand left the class untouched:
    **1,171 raw `sprintf` in this fork's own files and zero `snprintf`.**

    The bounded idiom already existed and was used 48 times:
    `sprintf2(S(sz), ...)` is `snprintf(sz, sizeof(sz), ...)` (astrolog.h:413),
    with `SO(pch, sz)` for writing at an offset. So this is a sweep onto
    something the codebase already had, not a new convention.

    **The one way it could do harm, and how that is excluded.** `sizeof` is
    only the array's size where the array's declaration is in scope. If the
    destination is a *parameter* -- `char *sz`, or `char sz[]`, which decays
    -- then `sizeof(sz)` is 8, and `sprintf2(S(sz), ...)` truncates every
    string to seven characters **while compiling without a warning**. So the
    transformer resolves each destination against the declarations of its
    enclosing function and of file scope, and converts only what it can prove
    is an array. Every site is classified, none guessed:

        array          1046   converted
        offset            9   converted, via SO()
        pointer-skip     11   destination is a char *
        PARAM-skip       27   destination is a parameter -- the dangerous set
        (never reached)  78   pointer arithmetic, struct members, split lines

    1,055 converted, **116 left for judgment**, and the parser reports zero
    unknowns. Writing it exposed a bug in the first version worth keeping in
    mind: it read only the first declarator of a statement, so
    `char szCity[cchSzMax], sz[cchSzMax], *pch;` hid `sz` and 77 sites came
    back "unknown". A declaration list is not a declaration.

    **`tools/chart-matrix.sh` is new, and this increment is why.**
    `tools/switch-matrix.sh` prints each run's stderr and the settings file
    it saves -- **it never renders a chart.** The whole of charts0-3.cpp,
    intrpret.cpp and the x*.cpp text paths sit outside it, which is exactly
    where these conversions land (charts1 204, io 200, intrpret 146, general
    82, charts0 75). The switch matrix came back byte-identical over 75,471
    lines while saying nothing whatever about them. The new harness runs
    every text chart the console build can draw over a pinned date -- single
    charts, seconds/sidereal/3D variants, relationship charts, and the
    interpretation text -- so two binaries can be byte-diffed.

    **Falsified**: shrinking one converted site's bound to 7 bytes moves the
    chart matrix off zero. It has teeth.

    Writing it also cost three lessons worth keeping. Its first version left
    15 of 70 invocations erroring on switches given the wrong arity (`-T`
    wants three arguments, relationship charts want two chart *files*, not
    two `-q` blocks) -- they byte-diffed identically, so the proof held while
    a fifth of the coverage was doing nothing. Its second version normalized
    the `mktemp -d` path in each run's output but not in the `== ` header
    line it echoes, so 48 lines of pure temp-directory noise looked exactly
    like a real behavioural diff. And restoring a sabotaged file with
    `git checkout` silently reverted that file's whole share of the sweep --
    204 conversions in charts1.cpp, found only because the totals stopped
    reconciling. Reverse-patch the sabotage instead.

    **Nets**: chart matrix 0 of 6,936 lines over 71 invocations, none of them
    erroring; switch matrix 0 of 75,471;
    suite 3524/0; settings round trip all three legs; six audits clean;
    three generated tables in sync; console, Qt, Qt-test and Windows builds
    all compile; `tools/asan-sweep.sh` 758 invocations, 0 reported.
    Behaviour is identical by construction wherever output
    already fit, which is everywhere these two matrices reach; where it did
    not fit, undefined behaviour became truncation.

    **What is left, and it is the sharper half.** The 27 PARAM sites are
    genuinely unbounded and cannot be fixed this way -- a function handed a
    `char *` cannot know the buffer's size, so those need a size parameter
    threaded from their callers, one at a time. That is where the remaining
    risk in T5 actually lives now. **Done in items 144-145 the same day** --
    this paragraph is left as written because the estimate was right about
    where the risk was: two of those sites turned out to be live stack
    smashes. `wdriver.cpp`/`wdialog.cpp` were left out of the sweep
    entirely: they are upstream-shaped Windows files and neither matrix can
    exercise them, so converting them would be unproven -- still true.

144. **T5's sharper half, first pass: eight functions stop formatting into
    a buffer whose size they do not know.** Item 143 bounded 1,055 sites
    mechanically and stopped exactly where `sizeof` stops being the
    answer: a destination that arrives as a `char *` parameter has
    `sizeof` 8, so the sweep would have truncated to seven characters
    while compiling clean. Those were left, counted, and are the work
    here. **Thirteen functions, 29 sites, down to five and eight.**

    **The convention, and it is not new.** The size follows the pointer,
    so a caller passes both with the macro the codebase already has:

        void SzObjSelName(char *sz, int cchMax, int nTyp, int nObj);
        ...
        SzObjSelName(S(sz), od.nTyp, od.nObj);

    `S(sz)` expands to `(sz), (int)sizeof(sz)`, which is exactly two
    arguments. Nothing had to be invented; the same macro that bounds a
    local `sprintf2` bounds a call.

    Converted: the object-name family (`SzObjSelName`, `SzObjDefFormat`,
    `SzObjSelDef`, `SwissGetObjName` -- four functions that nest, so the
    size threads through), `GetSzConstel`, `FJPLCacheGet`, and
    `FormatGridCell` with its ten sites. That is 14 call sites across
    calc.cpp, charts0.cpp, io.cpp, xcharts1/2.cpp, switch.cpp,
    qtdialog.cpp, qttest.cpp and **wdialog.cpp** -- these are this fork's
    own functions, so the Windows build takes the change too and compiles.

    **The size parameter is not decoration.** Callers of the object-name
    family pass `cchSzMax` twelve times and `cchSzDef` twice; a function
    assuming either would be wrong for the other.

    **One conversion needed no signature at all.**
    `SwissComputeStar()` writes into `pes->sz`, and `ES::sz` is
    `char sz[cchSzDef]` -- a sized array reached through a pointer, which
    `sizeof` sees straight through. `sprintf2(S(pes->sz), ...)` just
    works.

    **One is deliberately left unbounded, and says so at the function.**
    `WchToUTF8()` writes at most three bytes plus a terminator on every
    branch, and `wchar` cannot reach the four-byte range, so the bound is
    a property of the code rather than of the caller. A size parameter
    there would be noise across four call sites. The contract is now a
    comment instead.

    **This refactor has a net the sweep did not: the compiler.** Changing
    a prototype makes every missed caller a build error, where item 143's
    mechanical rewrite would have compiled silently and truncated. All
    four builds compiling *is* the coverage proof.

    **Nets**: suite 3524/0; chart matrix 0 of 6,936; switch matrix 0 of
    75,471; console, Qt, Qt-test and Windows builds all compile.

    **Still open, and they are coupled.** `FormatR` (23 callers) and
    `PchFormatExpression`/`PchFormatString` cannot be done separately:
    `FormatR`'s one non-array caller sits *inside* `PchFormatExpression`,
    which is itself handed a pointer walking through a caller's buffer.
    Worse, the three loops that call the `PchFormat` pair
    (`PrintSzFormat` and its two siblings) copy into that buffer
    unbounded on their own account -- `*pch2 = *pch` with no end check --
    so bounding the callee without bounding the loop would fix nothing.
    That is one increment, not three. `FileOpen()` (27 references, writes
    a path into `szPath`) is the other, and it is the one with an
    incident already: work log item 68, a deep install directory
    overflowing the ephemeris path at startup.

145. **T5's parameter half is finished, and two of the last three
    conversions were live stack smashes.** Item 144 took thirteen
    functions that format into a caller's buffer down to five. This takes
    them to **one**, and that one is the deliberate exception
    (`WchToUTF8`, whose bound is a property of the code and is now a
    comment at the function). **1,141 of the 1,231 formatting calls in
    this fork's own files are bounded**; the 90 left are pointer
    arithmetic, struct members and split lines, none of them a
    caller-owned destination.

    `FileOpen()` was the easy one: 20 call sites, fifteen passing `NULL`
    for the path and five a real `cchSzMax` buffer, so
    `FileOpen(szFile, 2, S(szPath))` and `FileOpen(szFile, 0, NULL, 0)`.
    It had an incident on record already -- item 68, a deep install
    directory overflowing the ephemeris path at startup.

    **The `FormatR` / `PchFormat` cluster is where the bodies were.** It
    had to be done as one increment because the coupling is total:
    `FormatR`'s single non-array caller is *inside*
    `PchFormatExpression`, which is handed a pointer walking through
    someone else's buffer, and the three loops that call that pair copy
    into that buffer **with no end check of their own** --
    `*pch2 = *pch`, forever. Bounding the callee alone would have fixed
    nothing.

    Two of those three loops are reachable from documented switches, and
    both killed the release build at HEAD:

        $ astrolog -YYt <3000 characters>
        *** stack smashing detected ***: terminated
        Aborted (core dumped)

        $ astrolog -YXt <3000 characters> -Xv 6 -Xo out.bmp
        *** stack smashing detected ***: terminated
        Aborted (core dumped)

    `-YYt` prints formatted text through `PrintSzFormat()` into
    `szFormat[cchSzLine]` (1020 bytes); `-YXt` sets the graphics sidebar,
    which a second loop copies through a local `sz[cchSzDef]` (80). Both
    are upstream's, in shared core, so the Windows build had them too.
    Both now exit 0 and truncate.

    The third, `FormatSz()`, takes its destination as a parameter, so
    `SO()` cannot help it -- the remaining space is
    `cchMax - (pch2 - szFormat)`, computed by hand.

    **Each of the three has its own falsified net, and they had to be
    three different kinds.** `FormatSz` has an inspectable destination, so
    it takes an in-suite assertion: a 3,000-character format into a
    100-byte buffer, asserting the result stays inside it. The sidebar
    needs a render, so it is an in-suite one too, driving `SetChartModeQt`
    with `gs.szSidebar` 3,000 characters long. `-YYt` can be neither: it
    ends in `PrintSz()`, which writes to `is.S`, a `FILE *` that only
    `Action()` opens -- so it is an invocation in
    `tools/switch-matrix.sh`, where its crash at HEAD is the whole
    behavioural diff for this increment. Reverting any of the three
    bounds makes its net abort with *stack smashing detected*, which for
    an overflow is the test working.

    **Three method notes, all of them mistakes made here first.**

    *A regression test can be the regression.* Calling `PrintSzFormat()`
    from inside the suite put characters into a stream nothing had
    opened, and glibc freed a backup buffer it never allocated -- so the
    suite began aborting **six runs in twelve**, and an hour went into
    bisecting a heap corruption that was the test. ASan named it in one
    run (`qttest.cpp:3071`) after gdb had only shown where it surfaced.

    *Check for the compiler's failure, not for a word.* The build check
    was matching `" error "`, which does not match `Error 1` -- so
    `placalc.cpp` failed to compile for a stretch (three `FileOpen`
    callers in a file excluded from the caller scan) while every check
    reported "ok" **against the stale binary left behind**. The compiler
    had been naming the exact missed callers the whole time. Match
    `: error:` and `^make.*\*\*\*`.

    *Do not put a check where it cannot fail.* `-YXt` was added to the
    switch matrix beside `-YYt` and produced no diff at all, because that
    harness never renders and the switch only stores a string. Measured
    rather than assumed, and removed.

    **Nets**: suite 3526/0, and 0 aborts in 12 consecutive runs against
    6 in 12 while the bad test was in; chart matrix 0 of 6,936; switch
    matrix diffs to exactly the crash that stopped happening -- two lines
    of *stack smashing detected* at HEAD replaced by the 164-line settings
    dump that invocation now produces; settings round trip all three legs;
    six audits clean; console, Qt, Qt-test, Windows and ASan builds all
    compile *(wrong about Windows -- it had not compiled since
    2026-08-29; see item 146)*. Every makefile carries `$(OBJS): astrolog.h extern.h`, so a
    prototype change really does rebuild everything -- worth checking,
    because 20 signatures moved.

146. **Nothing here had ever read a compiler warning, and under that
    cover the Windows build had been broken for three days.** The
    maintainer asked why `make` prints so many warnings. Counting them
    produced two findings, and the second is the serious one.

    **First, what the warnings are, because the obvious reading is
    wrong.** A clean console build prints **66** at the makefile's own
    flags and **220** with `-Wall`. Nearly every visible one sits on a
    `sprintf2(S(...))` call -- work log item 143's sweep -- which reads
    like the sweep introduced them. Measured against the pre-sweep
    commit (`012303b`), it did not:

        pre-sweep 012303b     HEAD before this item
        24 -Wformat-overflow=  2 -Wformat-overflow=
        21 -Wformat-truncation= 43 -Wformat-truncation=
        18 -Wunused-result     18 -Wunused-result
         3 -Wformat=            3 -Wformat=
        -- 66 --               -- 66 --

    Twenty-two sites moved class, one for one, and the total did not
    move at all. `-Wformat-truncation` exists only for the `snprintf`
    family -- it needs a bound to compare against -- so before the sweep
    those same lines were bare `sprintf` with no bound, and GCC had
    nothing to say. The sweep turned twenty-two *provable overflows*
    into bounded truncations and left the compiler naming exactly which
    buffers are still too small for their worst case. Item 143 said
    "where it did not fit, undefined behaviour became truncation" in one
    clause; this is the list it was talking about, and nobody had read
    it.

    Which is the real finding underneath: **GCC had been naming 24
    provable buffer overflows in this tree all along.** The build checks
    match `: error:` and `^make.*\*\*\*` deliberately, for the reason
    CLAUDE.md gives. Warnings fell straight through that gap, and no
    harness here has ever looked at one.

    **`tools/warning_audit.py` closes the gap.** It compiles all four
    builds clean with `-Wall`, normalizes every warning to (build, file,
    function, flag, message with numbers masked), counts duplicates, and
    diffs the result against `tools/warnings.txt`. The masking is not
    cosmetic: numbers move when a buffer is resized and line numbers
    move on every insertion above a site, so neither can anchor a
    ledger; the function name survives both. Builds that agree collapse
    into one row -- a shared-core warning is one fact, not three -- and a
    count that differs between builds splits back out, which is the case
    worth seeing because it means the warning depends on `-DQT` or
    `-DQTTEST`. It fails on a **removed** line as well as an added one,
    so the ledger cannot quietly overstate what is left.

    **Falsified in both directions on its first real use**, which is the
    only reason to believe it: it reported exactly the twelve sites item
    147 fixed as GONE, and the two that changed class as NEW.

    **Then the second finding.** Turning warnings on means taking `-w`
    out of `Makefile.win`. Doing that, the build stopped compiling:

        calc.cpp:1270:10: error: missing template arguments before 'bciCore'

    That is not a warning `-w` was hiding. `make -f Makefile.win` at
    HEAD, unmodified, **fails and produces no `astrolog.exe` at all**.
    mingw g++ 10 defaults to `gnu++14`, where `Borrow bciCore(ciCore);`
    -- class template argument deduction, which g++ 11 hands the Linux
    builds for free -- is an error. `-fpermissive` downgraded it to a
    diagnostic and `-w` then swallowed the message.

    **It has been broken since `9822152` (T1 move 2, 2026-08-29) -- 62
    commits and three days** -- the commit that introduced the `Borrow`
    template and its first three uses in io.cpp. Work log items 143, 144
    and 145 each list "Windows builds" among their nets. **All three
    were wrong.** The behavioural oracle, the thing Windows parity is
    measured against, was dead the whole time, and the check that should
    have caught it is the one item 145 wrote its own lesson about:
    *check for the compiler's failure, not for a word*. The lesson was
    right and got applied to the Linux builds only.

    Fixed with `-std=gnu++17` in `Makefile.win`, plus the three
    `-Wno-write-strings`/`-narrowing`/`-comment` flags that are all `-w`
    was ever doing. With those the Windows build reports **zero**
    warnings at default level, compiles, runs under Wine, and
    `tools/win-tests.sh` passes both scenarios again.

    **One thing that build cannot do, measured rather than assumed.**
    mingw redirects `snprintf` to `__mingw_snprintf`, which GCC does not
    recognize as the builtin, so its format analysis is silently absent
    there: the same tree reports 45 format-truncation warnings under
    g++ 11 and 0 under mingw g++ 10. Turning warnings on there is still
    worth it -- it is the only diagnostic wdriver.cpp and wdialog.cpp
    have ever had, and it found two real ones (item 147) -- but it is
    not the net for that class.

    **Where the ledger stands after item 147: 819 warnings in 198
    sites** -- console 210, qt 220, qt-test 225, win 164. Six classes
    are now empty. What is left is `-Wmaybe-uninitialized` 91,
    `-Wparentheses` 55, `-Wformat-truncation=` 45, the unused-* family
    50, `-Wunused-result` 18 and `-Wsign-compare` 15: four separate
    campaigns, none of them a sweep. See "What to do next".

147. **The twelve warnings the compiler was right about.** Item 146's
    first pass took the classes where a diagnostic is nearly always a
    defect rather than a style note. Twelve sites, six classes, all but
    two in shared core, so the Windows build takes them too.

    **A memory leak nobody could see, because the test was against
    itself.** `FinalizeProgram()`, astrolog.cpp:698:

        if (szLifeArea[i] != szLifeArea[i])      // -Wtautological-compare
          DeallocateP((char *)szLifeArea[i]);

    The two lines above it read `szDesc[i] != szDescDef[i]` and
    `szDesire[i] != szDesireDef[i]`. This one compares a pointer to
    itself, is always false, and so never frees a life-area string that
    `-YIC` replaced. **The switch matrix proved it:** across 75,635
    lines the entire behavioural diff for this whole increment is one
    line disappearing, under the invocation `-YIC 7 partnerships` --

        - Number of memory allocations not freed before exiting: 1

    **Two provable overflows, both bounded now.** `GetJPLHorizons()`
    formatted up to 1,019 bytes of `szLine` into the 870-887 bytes left
    in `szUrl[cchSzLine]`; `PrintHorizonLine()` built its format string
    into `szFormat[cchSzDef]` unbounded. Both are item 143's "split
    lines" bucket -- multi-line string literals its parser never
    reached. `-Wformat-overflow=` is now **0** across all four builds.

    **Six varargs type mismatches.** `SzColor`, `SzColor2` and
    `SzColorHTML` passed a `dword` (`unsigned long`, 8 bytes on LP64) to
    `%x`, which reads 4. `FRedraw()` in wdriver.cpp passed `long` to
    `%d` twice, and `GetURL()` passed an `HRESULT`. Fixed with casts
    rather than length modifiers on purpose: `dword` is 8 bytes on Linux
    and 4 on Windows, so `%lx` would be wrong on one of them. The two in
    wdriver.cpp are the first diagnostics that file has ever received.
    `-Wformat=` is now **0**.

    **A draw that only looked guarded.** xcharts2.cpp:638, in
    `XChartWheelMulti()`:

        if (fHexa)
          rT -= off;
          DrawCircle(cx, cy, ...);      // runs either way

    With `fHexa` clear this redrew the circle from two lines above at an
    unchanged `rT` -- pixel-identical, which is why it never showed. The
    braces make it say what it does. **Netted with a bitmap
    differential** over `-r3` through `-r6`, proven pairwise distinct
    first (four different md5s) and then sabotage-proven: changing
    `rT -= off` to `off*2` inside the block moves `-r6` and nothing
    else. All four byte-identical before and after the fix.

    **And three the compiler was right about in the weaker sense** --
    correct code that reads wrong. `NParseSz()`'s hex-colour loop had
    its byte-swap indented as though inside the loop; `XChartIndian()`
    had `!FBetween(...) == us.fIndian` with the precedence it wanted but
    not the parentheses; `FCreateGrid()` and `FCreateGridRelation()` had
    an unbraced `if` wrapping an if/else chain. All three are
    behaviour-preserving by inspection and by the matrices.

    **Nets**: suite 3526/0; chart matrix **0 of 6,936**; switch matrix
    **2 of 75,635**, being exactly the leak that stopped happening;
    multi-wheel bitmap differential identical over four modes, sabotage
    proven; settings round trip all three legs; six audits and three
    generated tables clean; `tools/win-tests.sh` 2 scenarios; console,
    Qt, Qt-test and Windows builds all compile -- and this time that
    claim was checked rather than assumed (item 146).

148. **The unused-* campaign, first pass: 30 dead locals deleted, and a
    graphics differential built to prove it.** Item 13's first tranche.
    GCC named 50 `-Wunused-*` sites; this takes the family from 50 to 20,
    and the interesting part is what the reading turned up rather than
    the deletions.

    **Every one was residue, not a forgotten computation.** The campaign
    note predicted an assigned-but-never-read variable would sometimes be
    a computation somebody dropped. Read one at a time, all thirty were
    the other thing:

    - `FProcessSwitches()` still declared `j`, `k`, `rT`, `ch2`, `pch`,
      `ci` and `ppch` -- the locals of the four parsers phase 2's
      migration dissolved (M4-M6). Seven names, one of them still being
      assigned every iteration.
    - `temp = grid->n[x][y]` in `ChartGrid()` and `ChartGridRelation()`
      went dead when cell formatting moved into `PrintGridCell()`, which
      reads the grid itself.
    - `helioz[]` in `ComputePlanets()`: a full array of heliocentric z
      coordinates, computed and never read, because the retrograde test
      it feeds is a 2D cross product in the ecliptic plane.
    - `rRatio4` in `HousePullenSinusoidalRatio()` belongs to the `#else`
      branch's empirical solver; the analytic branch that is actually
      compiled solves the quartic in closed form and never needs it.
      Moved to the branch that uses it.
    - `TELE te` in `XChartLocal()` -- a projection context filled in with
      four fields and never projected through, copied from
      `XChartTelescope()`.
    - `radiM` in `XChartTelescope()`: the umbra and penumbra cones need
      the Sun and Earth radii and the two distances; the Moon's own
      radius never enters the maths.
    - and `xs`/`ys`, `zGlyph2`/`zGlyphS`, `xpEar`/`ypEar`, `unit`,
      `fNext`, `ch1Prev`, `szPath`, `cColon`, `isz`, `k`, `buttonx`/
      `buttony`, `pch`.

    One is not a deletion. `xLast`/`yLast` in `FBmpDrawBack()` cache the
    background bitmap's size, and only the Windows path reads them back;
    elsewhere they were written and dropped. Both the declaration and the
    reset are `#ifdef WINANY` now, which is why that row said
    `console+qt+qt-test` and not `win`.

    **`tools/graphics-matrix.sh` is new, and this increment needed it.**
    Eleven of these sites are in `xcharts0-1.cpp`, `xdevice.cpp` and
    `xscreen.cpp` -- drawing code. The switch matrix never renders and
    the chart matrix renders only *text*, so between them the whole
    graphics surface had no differential at all; phase 2's P6 built one
    by hand for a single switch family (items 102-104) and it was not
    kept. This is that idea, kept: 224 renders over every chart mode,
    every option on three chart types that draw differently, and every
    output writer, printed one checksum per render. Ten seconds a run.

    Three things it cost to get right, all of them the traps this project
    already has written down:

    - **A shell variable collision made 15 renders re-run the previous
      output file name as an argument.** They errored, produced no file,
      and would have compared equal forever. Hence the `MISSING` marker
      and the "N produced no file" tail -- a harness whose invocations
      all fail diffs to zero and reads exactly like a proof.
    - **Six renders differed between two runs of the same binary.** The
      PostScript writer puts its output path in a `%%Title` comment, and
      `mktemp -d` made that path new each time. Fixed paths now. Verify
      determinism before trusting a clean diff; that check is in the
      header.
    - **Three invocations inherited from `tools/asan-sweep.sh` were out
      of range** (`-Xw 200 150`, `-XI0 100 2`, and the `-XM1/3/6` prefix
      forms). That sweep only asks whether memory was corrupted, so an
      erroring invocation costs it nothing; here it is dead coverage.

    **Falsified**: perturbing `nScl` in `DrawMap()` moves 55 of the 224
    renders; reversing it returns an exact zero.

    **Nets**: suite 3526/0; chart matrix 0 of 6,936; switch matrix 0 of
    75,635; graphics matrix **0 of 224 renders**; all four builds
    compile. Warning ledger 819 -> 707, sites 198 -> 168.

    **Two left standing on purpose, with their verdicts.**

    `CommandLineX()` (xscreen.cpp) sets a local `fPause` on a switch
    error and on a multi-chart run, and then returns without reading it.
    The intent is legible -- pause so the user can read the error before
    the graphics screen comes back -- and supplying it would change what
    the X11 build does. That is a behaviour decision, not a warning
    cleanup, so the variable stays and this note is the record.

    `SwissHouse()` discards the return of `swe_calc_ut()` when computing
    the Sun's declination for Sunshine houses, and `xp[6]` is
    uninitialized above it -- so a failed call feeds a garbage
    declination into the cusps. That is the same class as the eighteen
    unchecked `fgets` calls item 13 parks separately, and it belongs with
    them rather than here.

    **And one found by reading rather than by the compiler.**
    `RedrawTextQt()` in qtdriver.cpp is defined and never called: the
    port used to show text charts in their own `QTextBrowser` window, and
    `RedrawQt()` now draws them into the canvas the way Windows does.
    The function, the `qi.pdlgText`/`qi.ptextBrowser` fields and two
    no-op guards are all orphans of that change -- and three comments in
    qtdriver.cpp plus two passages in this file and REFACTORING.md still
    describe the old design as live. The right-click path is fine
    (`ShowContextMenu()` chooses `PmenuContextForTextQt()` off
    `us.fGraphics`), so nothing is broken; it is dead code plus stale
    documentation, and it gets its own increment.

149. **Four classes closed at once, and the reading found a Qt command
    that had silently done nothing for months.** Item 13's remaining
    cheap tranches taken together: `-Wsign-compare`, `-Wparentheses`,
    `-Wunused-result`, and the tail of `-Wunused-*`. Ledger 707 -> 462,
    sites 168 -> 131.

    **Clear Screen was dead in text mode, and the compiler only pointed
    at the door.** `RedrawTextQt()` showed as `defined but not used`.
    Following it: the port used to draw text charts into their own
    `QTextBrowser` window, and `RedrawQt()` has since drawn them into the
    canvas the way Windows draws into its client area. The function, the
    `qi.pdlgText`/`qi.ptextBrowser` fields, `AddHotkeysToWindowQt()` and
    a `hide()` guard are all orphans of that change -- and so was
    `ClearTextWindowQt()`, which `ClearScreenQt()` still called for text
    mode. It cleared a window that is never created, so **Clear Screen
    did nothing at all in text mode**, while Windows' `TextClearScreen()`
    clears its client area. One path serves both now, because both draw
    into the same buffer; `gi.kiOff` is `kMainA[fInverse]`
    (xscreen.cpp:162), exactly the colour Windows passes to
    `WinClearScreen()`.
    Three comments in qtdriver.cpp still described the retired design as
    live; they are corrected.

    The new `clear-screen` group is 8 assertions, and **reverting the fix
    fails its text half**. Getting it right took two attempts, both
    instructive:
    - The first version rendered a chart and asserted something had been
      drawn. It passed alone and failed in the full run, because the
      chart inherits whatever restrictions and flags earlier groups left
      set -- this file's own header trap, and item 141's diagnosis.
    - The second version still called `RedrawQt()` in *text* mode, which
      runs `Action()` with `is.S` pointed at stdout. That made the
      `long-strings` group fail **two runs in three** with the same "0
      bytes" signature as the intermittent item 141 recorded and could
      not reproduce. Painting the dirt directly and never redrawing fixed
      it: four consecutive clean full runs. The lesson is item 145's,
      again -- a regression test can be the regression.

    **`-Wunused-result`: the reads whose failure nobody noticed.** Each
    of the six atlas loops reads exactly as many records as a count
    earlier in the same file claims. When the file is short, `fgets`
    fails, `szLine` still holds the *previous* record, and the loop
    parses it again -- once per missing row. Measured on an atlas
    truncated to 500 lines:

        HEAD: Astrolog: Zone rule error: Day operator not >= or <= in
              entry 5 of rule 11: ''
        now:  Astrolog: Atlas error: File ended after 483 of 33702 cities.

    The old build re-parsed one line 33,219 times and then blamed the
    *time zone rules*, a different subsystem entirely. Same treatment for
    `FReadSzLineTrim()` (a read error that was not EOF fell through with
    the previous line), the old-style `-o` chart info and `-o0` position
    readers, and the exoplanet list. Two are left on purpose: the
    exoplanet line-counting loop, where `i` is load bearing (`cexod =
    i-2`) and moving where it stops would change how many planets the
    list holds, and placalc.cpp, which is third-party.

    **`-Wsign-compare` was a type that had been wrong all along.**
    `GetXY()` is declared `KI` but returns `_GetXY()` -- a packed 24 bit
    value -- in bitmap mode, and the flood fill compared it against a
    `KV`. The values were right, both being non-negative; the types were
    not. `GetXY()`/`SetXY()` carry a `KV` now, which is what they always
    carried. The other half was `rgod[].kv == ~0`: a KV tested against a
    signed int at thirteen sites, with no name for the sentinel. It has
    one now -- `kvNone` in astrolog.h -- which is theme T2's "none is
    spelled three ways" closed in miniature.

    **`-Wparentheses`, all 24, were correct code that reads wrong.**
    Twelve `if (x = f())` assignments-as-conditions, `(x)&1^1` and
    `(x)*3 + 3 & ~3` in the bitmap macros, `rgx[i+1 & 3]`,
    `(sign-1 & 3)^...`, and two `==` chains of exactly the shape item
    147's `XChartIndian` find had: `(j < 1 == us.fArabicFlip)` and
    `(a >= q) == (b >= q) == fFlip`. Every one means what it already
    did; the parentheses only make GCC agree. placalc.cpp's is left,
    third-party.

    **Nets**: suite 3534/0 across four consecutive runs; chart matrix 0
    of 6,936; switch matrix 0 of 75,635; graphics matrix 0 of 224;
    settings round trip; six audits and three generated tables;
    `tools/win-tests.sh` 2 scenarios; all four builds compile.

    **What the ledger still holds, with verdicts.**
    `-Wmaybe-uninitialized` (91) and `-Wformat-truncation=` (45) are the
    two real campaigns left and neither is a sweep.
    `-Wunused-function` (9) is qtdialog.cpp's documented dialog helper
    toolkit -- `NewComboQt`, `PlaceRowQt`, `HeadersQt` and the rest,
    which QT_GUI_PLAN.md tells the next session to use rather than
    reinvent; they are a library with no current caller, not dead weight,
    and deleting them would contradict that advice.
    `-Wunused-value` (4) is `FreeProcInstance()` in wdialog.cpp, a Win16
    relic that expands to its own argument on Win32.
    `-Wunused-result` (4), `-Wsign-compare` (2) and `-Wparentheses` (1)
    are third-party (placalc.cpp, sweph.cpp) plus the exoplanet counting
    loop.
    `-Wunused-variable` (1) and `-Wunused-but-set-variable` (1) are the
    two item 148 left standing with their reasons: `SwissHouse()`'s
    discarded `swe_calc_ut()` return and `CommandLineX()`'s `fPause`.

150. **The last two classes get verdicts rather than edits, and the
    evidence took one measurement instead of ninety-one readings.**
    Item 13's remaining campaigns were `-Wmaybe-uninitialized` (91) and
    `-Wformat-truncation=` (45). Neither produced a code change, and
    that is the finding, not a shortfall: item 13 predicted the first
    would be mostly analysis artifacts and warned that initializing a
    variable to silence one **converts a real bug into a confidently
    wrong answer**. It was right, and here is what says so.

    **`-Wmaybe-uninitialized`: GCC never proved one, and the set moves
    with `-O`.** Compiling the whole shared core three ways:

        -Wuninitialized  (GCC *proved* a use before set)      0
        -Wmaybe-uninitialized at -O                          65
        -Wmaybe-uninitialized at -O2                         58

    Not one definite case in the tree. And the two "maybe" sets are not
    the same set: **15 warnings appear only at `-O` and 8 only at `-O2`**
    -- `dRing`, `nObj`, `rSav`, the `xd2`/`yT3` line-drawing pairs on one
    side, `fSav`, `nSav`, `rT` on the other. A defect does not appear and
    disappear with an optimization flag; a path analysis that cannot
    correlate two conditions does. (`-O0` reports zero, but that proves
    nothing -- GCC documents that this warning needs optimization to
    exist at all. The evidence is the *proved* count being zero and the
    set being unstable, not the `-O0` figure.)

    Three read by hand, each a false positive for a different reason, and
    each worth knowing because the shapes recur:

    - `FReadSzLineSkip()` (io.cpp): `ch` is only read after a loop that
      exits either on EOF -- which returns first -- or on having assigned
      it. Two conditions GCC cannot correlate.
    - `InterpretEsoteric()` (intrpret.cpp): the `switch (bod)` has cases
      0-4 and `bod` runs `cRayArea-1` down to 0, with `cRayArea` 5. An
      exhaustive switch that GCC does not know is exhaustive.
    - `FSortCIList()` (general.cpp): `nMethod` is 0-5 from the switch
      surface and `GetRadio(hdlg, dr01, dr05)` on Windows, and the switch
      covers exactly that. Exhaustive over the reachable domain -- though
      this one is *fragile* rather than wrong: a sixth sort method added
      without a case would read `fCompare` uninitialized.

    **The largest sub-family is a worklist for a theme that already
    exists.** Eight of the sites are `nSav`/`kSav`/`rSav`/`fSav` --
    a value stashed under one condition and restored under another,
    which is REFACTORING.md's T7 and exactly what phase 2's P6 converted
    to the `Borrow` template (work log items 102-104). GCC's list is an
    independent census of the hand-rolled save/restore pairs P6 left
    behind, and the right time to act on these is when T7 is worked, not
    as a warning sweep. Cross-referenced at T7.

    **`-Wformat-truncation=`: every one is bounded on purpose, and the
    interesting ones are pinned by a test already.** Item 143 turned
    1,055 unbounded `sprintf` calls into bounded ones; this is GCC
    naming the buffers whose theoretical worst case still does not fit.
    Read, the 44 sites fall into three groups and none is a defect:

    - **Error messages that quote the offending input** (11 in atlas.cpp,
      plus `general.cpp`'s `FErrorValR`): "Zone rule error: ... '%s'"
      with a whole 254-byte line going into what is left of a 255-byte
      buffer. Truncating the quoted line in a diagnostic is the intent.
    - **Truncation points that are asserted** (io.cpp 542/544/570/572 and
      942, the AAF and ADB import joins): the `file-parsers` group
      already pins them -- "AAF 400-char name and location truncate to
      %d", "ADB 79-char city and country join truncates to %d" -- from
      work log items 118-120. These are the sites that used to *overflow*
      and crash a user's chart. They now truncate, at a tested point.
    - **Worst cases the values cannot reach**: `%d` of an hour, `%03d` of
      a millisecond, the sidebar and credits lines. GCC assumes `%d` of
      an `int` can be 11 characters; the value is a clamped 0-59.
      Where truncation *is* reachable it fails visibly rather than
      silently -- the ephemeris path prints "longer than %d characters,
      so truncated" two lines later (calc.cpp), a truncated `-id`
      filename fails to open, a truncated JPL URL fails to fetch.

    So the ledger keeps all 45, and REFACTORING.md's T5 says why.

    **Where this leaves the ledger: 462 warnings in 131 sites, and every
    class in it now carries a recorded verdict** -- these two, plus item
    149's (qtdialog's documented helper toolkit, `FreeProcInstance()`'s
    Win16 no-op, the third-party files, the exoplanet counting loop) and
    item 148's two (`SwissHouse()`'s discarded return,
    `CommandLineX()`'s `fPause`). Started at 857 in 209 sites four
    increments ago. `tools/warning_audit.py` fails on any addition, so
    the next warning to appear in this tree will be a new one.

151. **What the maintainer's build actually prints, which the campaign
    had not been measuring.** Items 146-150 worked the `-Wall` ledger and
    got it from 857 to 462. The maintainer's response was "still getting
    tons of warnings on build", and they were right: an ordinary
    `make` does not use `-Wall`, so what reaches the terminal is the
    *default* set -- which was dominated by the one class item 150 had
    given a verdict to instead of a fix.

    The measurement nobody had taken: **a clean `make` printed 49
    warnings in 722 lines of output**. The count understates it badly,
    because every `-Wformat-truncation` drags five lines of glibc
    `stdio2.h` `note:` behind it. Roughly 270 of those 722 lines were one
    warning class.

    **It is now 12 warnings in 182 lines**, and no behaviour moved.

    **The fix was sizing, and the shape recurs everywhere.** Almost every
    site was a buffer declared the same width as the field it holds, and
    then written with a label in front:

        char sz[cchSzDef], szT[cchSzDef];
        ...
        sprintf2(S(sz), "Special: Delta-T = %s", szT);

    That can never provably fit, whatever the values are. Twenty
    destinations got room for what their own format can emit -- the
    `sz`-with-a-label family in charts0/charts1/charts3/xcharts0/io, the
    three zone loaders' `szErr`, `FLoadAtlas`'s own error buffer (which
    it was building inside the line it quotes from), `FErrorValR`'s, and
    six formatted-field statics in general.cpp that were sized for the
    values the program produces rather than for what `%d` of an `int` can
    emit. Nothing here changes a single character the program prints:
    these formats already fit for every value it produces.

    **Two enlargements had to be reverted, and both are worth knowing.**
    Growing `GetJPLHorizons()`'s `szUrl` only moved the warning to
    `FJPLCachePut()`, which copies it into a `cchSzLine` struct field
    behind a length guard GCC cannot see -- one warning either way, so it
    stays on the site that actually truncates. And in
    `DisplayTimezoneChanges()`, `sz` and `sz1` hand their contents to
    each other with a prefix added *each way*, so whichever one is made
    larger makes the other unable to fit. That pair cannot be sized
    apart; it is left as it was.

    **The twelve that remain, each with a reason.** Five are the AAF and
    ADB import joins whose truncation points the `file-parsers` group
    *asserts* (work log items 118-120) -- enlarging those buffers would
    change what the importer keeps, which is a behaviour decision, not a
    warning fix. One is the JPL URL above. One is the `sz`/`sz1` pair.
    One is the ephemeris path, which already prints "longer than %d
    characters, so truncated" two lines later, so the truncation is
    reported rather than silent. The last four are `-Wunused-result`:
    three in placalc.cpp, which is third-party, and the exoplanet
    line-counting loop whose loop variable is load bearing (item 149).

    **The lesson is about where a net points.** `tools/warning_audit.py`
    deliberately compiles with `-Wall` so it sees more than an ordinary
    build ever will -- which is right for catching regressions, and
    exactly wrong for knowing what the person running `make` has to read.
    The audit's own count went 462 to 354; the number that mattered went
    49 to 12, and it was not in the ledger at all. It is now: the header
    of `tools/warnings.txt` records both.

    **Nets**: suite 3534/0; chart matrix 0 of 6,936; switch matrix 0 of
    75,635; graphics matrix 0 of 224; settings round trip; six audits;
    `tools/win-tests.sh` 2 scenarios; all four builds compile.

152. **All four builds compile silently.** Item 151 got an ordinary `make`
    from 49 warnings to 12 and stopped at the ones with reasons. The
    maintainer's answer was to keep going until it is clean, so the
    remaining twelve were taken one at a time rather than dismissed.
    **`make`, `make -f Makefile.qt`, `make -f Makefile.qt.test` and
    `make -f Makefile.win` now each print zero warnings**; a console
    build is 32 lines of output where it was 722.

    Five of the twelve were the same defect, and it was worth the trip.
    **The AAF and ADB importers assembled two fields into a buffer no
    wider than one of them.** `FProcessADBFile()` joined a city and a
    country -- `cchSzDef` each -- into a `cchSzDef` buffer, so a long
    city name **discarded the country outright**; `FProcessAAFFile()` did
    the same with name and location into a `cchSzMax` buffer. Both fields
    are slices of one `cchSzLine` input line, so the buffers are
    `cchSzLine*2` and `cchSzMax` now and a pair that fits on an input
    line survives whole. This is the same code the long-line net first
    reached in work log item 118, where the bug was that the two
    assembly `sprintf`s were *unbounded*: bounding them stopped the
    crash and pinned the truncation, and this finishes the job by
    removing the truncation.

    **The `file-parsers` assertions moved with it, deliberately.** They
    read "AAF 400-char name and location truncate to 254" and "ADB
    79-char city and country join truncates to 79"; they now read
    "survive whole (406, 416)" and "keeps both (160)", with the
    arithmetic spelled out at each. This is the one behaviour change in
    the batch and it is the point of it. Falsified: putting the ADB
    buffer back to `cchSzDef` fails the new assertion with 79.

    The other seven:

    - **`DisplayTimezoneChanges()`** builds `sz` from `sz1` in one block
      and `sz1` from `sz` in another, so neither could be widened without
      breaking the other. The two dialog rows that close the cycle got a
      buffer of their own.
    - **`SwissEnsurePath()`**'s `szPath` is `AS_MAXCH` because
      `swe_set_ephe_path()` wants that width, so the destination cannot
      grow; an explicit `%.*s` precision states the truncation snprintf
      was already doing, in a form the compiler can check.
    - **`GetJPLHorizons()`**'s URL and the cache entry it is copied into
      grow together, so the length guard between them stays meaningful.
    - **`ChartExoplanet()`**'s line counter ran one extra iteration -- the
      one whose `fgets` failed -- which is what its `-2` compensated for.
      It counts the lines it actually read now, and `-1`. Proven
      equivalent on the shipped `astexo.csv` (4,545 lines, ends in a
      newline): the `-Ux` listing is byte-identical, 271 lines.
    - **placalc.cpp**'s three `fread` returns are third-party
      (REFACTORING.md non-goals) and stay unreviewed, behind a
      `#pragma GCC diagnostic` with the reason at the site.

    **Nets**: suite 3534/0; chart matrix 0 of 6,936; switch matrix 0 of
    75,635; graphics matrix 0 of 224; the `-Ux` exoplanet listing and the
    `-N`/`-Nl`/`-Nz` atlas listings byte-identical; settings round trip;
    six audits; `tools/win-tests.sh` 2 scenarios; four builds, all
    silent.

    The `-Wall` ledger is 318 warnings in 101 sites, down from 857 --
    every one of them invisible to an ordinary build, and every class
    carrying a verdict (items 148-151). `-Wformat-truncation=` went 45 to
    1 and `-Wunused-result` 18 to 0 on the way here.

153. **The oracle gets three invariants that need no reference, and the
    parked house findings turn out to name the wrong systems.** Item 11's
    cheapest tranche: the properties the program must satisfy whatever
    the numbers are, which is T9's argument in its least expensive form.
    Three legs, seven assertions, all four of them falsified.

    - **An aspect is the same aspect from either side.** `GetAspect(i, j)`
      and `GetAspect(j, i)` ask about one pair of points, and
      `FCreateGrid()` depends on their agreeing -- it fills half the grid
      from `(x,y)` and reads the other half as `(y,x)`. 465 pairs, aspect
      and orb compared both ways. Nothing had ever checked it.
    - **A midpoint is equidistant from both sources and between them.**
      `Midpoint2()` chooses between two candidate points 180 apart, which
      is exactly where a sign error hides. 1,716 pairs swept.
    - **A progressed chart at zero elapsed time is the natal chart.**
      `CastChart()` adds `(JDp - T) / rProgDay` to the chart time, so a
      progression whose target date *is* the natal date must land back on
      the natal sky. Nothing else in the suite exercises the progression
      path at all.

    **Falsified, each against its own defect**: perturbing `GetOrb()`'s
    limit fails aspect symmetry (1 of 465 -- a boundary flip, which is
    the sensitivity you want); perturbing the orb the function *returns*
    fails the orb half (91 of 465); disabling `Midpoint2()`'s
    half-circle correction fails 435 of 1,716; adding a day to the
    progression fails 27 of 31 objects. The orb half needed a second,
    different sabotage -- the first one only moved the acceptance
    boundary and left the returned orb alone, so that assertion was
    passing unproven until it was aimed at properly.

    **And a correction to item 141 -- WHICH WAS ITSELF WRONG. See item
    157.** The paragraph below stands as written because the mistake is
    the point: it swept 40 systems and 8 latitudes but only **one date
    and one longitude**, and on that sample the findings genuinely do not
    appear. With four dates they reproduce on both engines, along with
    four more systems. Item 141 was right.

    That item parked two measured findings: "Topocentric houses run
    backwards beyond the polar circle" and "Pullen (S.Delta) produces
    zero-width houses at 70N and above". Re-measuring the partition over
    all 40 systems at 45N through 88N, on the Swiss house path that
    `nrvate.as` selects, **neither reproduces** -- Topocentric closes the
    circle at every latitude and Pullen S.Delta has no zero-width house.
    What does misbehave is **Sunshine, which has a zero-width house at
    70N and above**, and which item 141 does not mention.

    The likely explanation is that there are two house engines and the
    note does not say which it measured: `SwissHouse()` runs when
    `us.fEphemFiles && !us.fPlacalcPla` and lets the library substitute
    at extreme latitude, while Astrolog's own `ComputeHouses()`
    (calc.cpp:502) runs otherwise and guards **only** Placidus and Koch
    (calc.cpp:508). So the finding is probably real on the Matrix path
    and was written down without the qualifier. **Not resolved here**:
    the both-engines sweep is a slow measurement and the fix is a
    maintainer decision about house math in both builds. What this item
    contributes is that the record as written is wrong, and that leg 4 of
    the oracle -- which runs at one mid latitude by design -- would have
    caught the discrepancy years earlier if it swept latitude and engine.
    That is the next increment for item 11.

    **Nets**: suite 3541/0 (up 7). Test-only change; the console binary
    does not compile qttest.cpp, so the matrices have nothing to say
    about it and were not run.

154. **T7's blessed capture helper, and the bug it was written to
    prevent was already there.** REFACTORING.md's T7 proposes "one
    blessed capture helper in shared code -- render current chart to this
    file/buffer with these dimensions, touching nothing -- built once
    from the dance `FExportChartQt()` already does correctly, then used
    by every capture site". Read against the code, half of that is wrong
    and the other half found a live defect.

    **The shared-code half does not survive inspection.** Windows does
    not do this dance at all. `DlgSaveChart()` (wdialog.cpp:555) only
    *arms* the state -- `gs.ft = ftBmp; us.fGraphics = wi.fRedraw =
    fTrue;` -- and lets the next redraw perform the export, saying so:
    "Saving actual chart output isn't done until the next redraw."
    `wdriver.cpp`'s autosave is a third shape again, re-running
    `Action()` after the screen draw and restoring by *dividing* what it
    multiplied. The Qt port cannot use the redraw-driven form, which is
    why `FExportChartQt()` is synchronous with an explicit save/restore.
    That is a deliberate divergence, not duplication, and a shared helper
    would have exactly one caller. Recorded rather than done, the way
    items 87 and 88 closed T3's harvest.

    **The Qt half was real, and it was broken.** There are two text
    captures in the port and they are the same dance:
    `CaptureTextChartQt()` in qtdriver.cpp and `ShowExportTextDialogQt()`
    in qtdialog.cpp -- the File menu's **Export Chart Text Output**. The
    first saves and restores three globals; the second saved two.

    The missing one is `is.S`, and the reason is not obvious, which is
    why one copy had a paragraph about it and the other had nothing.
    `Action()` (astrolog.cpp:151) opens `is.S` on `is.szFileScreen` and
    `fclose()`s it at the end (astrolog.cpp:338) **without putting the
    caller's back**. The whole GUI runs inside an `Action()` call already
    (main -> Action -> FActionX -> InteractQt), so an export is a *nested*
    one: afterwards `is.S` points at a closed `FILE`, everything printing
    through it writes to a dead handle, and the outer `Action()`
    `fclose()`s the same handle a second time on the way out, which
    glibc catches as "invalid stdio handle" and aborts on.
    `NPromptSwitches()` (astrolog.cpp:464) saves and restores it around
    its own nested call for exactly this reason.

    **Reproduced before it was fixed**, in the probe: `is.S == stdout` is
    true before Export Chart Text Output's dance and false after.

    `CaptureTextToFileQt()` in qtdriver.cpp is that dance, once, with the
    three globals and the reason for each at the definition. Both callers
    use it. The export dialog passes `us.fTextHTML` through rather than
    forcing it, so the File Settings "Export as HTML" choice still
    decides and nothing else about the export moves.

    New `text-export` group, 4 assertions, and it puts `is.S` back by
    hand after checking it so a regression fails this group instead of
    taking the rest of the suite down. **Falsified**: deleting the
    `is.S` restore fails it.

    **And a correction to yesterday's claim.** Item 152 said all four
    builds compile silently. That was measured with an *incremental*
    `make -f Makefile.qt`, which never recompiled `qtdialog.o` -- the
    exact trap CLAUDE.md names ("if a check claims to have rebuilt
    something, look at the binary's timestamp"). Touching qtdialog.cpp
    here surfaced a format-truncation warning that had been there the
    whole time. Fixed, and all four re-verified with `make clean` first.

    **Nets**: suite 3545/0; four clean builds, zero warnings each; four
    audits and the settings round trip; ledger 318 -> 316. Qt-only
    change, so the console matrices have nothing to say about it.

155. **T6's refactor has nothing left to do, and the audit it prompted
    found a rendering gap instead.** T6 asks to "push the remaining
    in-function `#ifdef`s down into those bottlenecks so each backend is
    one block per primitive, not confetti", citing 52 backend
    conditionals in xgeneral.cpp and 44 in xscreen.cpp.

    **Re-measured, attributing every one to its enclosing function**
    (51 and 44 today, so the metric matches):

        xgeneral.cpp  51  DrawSz 8, DrawDash 5, DrawPoint 4, DrawBlock 4,
                          DrawColor 3, DrawArc 3, DrawEllipse2 3,
                          KiCity 3, DrawThick 2, DrawClearScreen 2,
                          DrawFill 2, file scope 4
        xscreen.cpp   44  InteractX 32, BeginX 4, InitColorsX 2,
                          file scope 6
        xdevice.cpp   23  BeginFileX 5, FBmpDrawBack 4, FBmpAntialias 4,
                          FBmpDrawMap 3, FBmpDrawMap2 3, others 4

    **There is no confetti.** Every branch in xgeneral.cpp is already
    inside a drawing primitive -- which is what E1 closed by shape audit
    on 2026-08-29 (work log item 86) -- and 32 of xscreen.cpp's 44 are in
    `InteractX`, the event loop that E3 says is inherently per-backend
    and stays. There is nothing to push down; the destination is where
    they already are. Closed by measurement, like T3's harvest and T7's
    shared half.

    **But the audit was not wasted.** E1 also claimed "everything above
    them, all of xcharts*, is target-free". That is **not true**: there
    are 11 backend conditionals in the chart layer, and the shape T6
    names as its incident class -- a `WIN`-only branch with no QT twin --
    is exactly what they are. Seven are understood divergences already
    recorded elsewhere (the Windows-only `gTraTraTim`/`gTraNatInf` chart
    modes, `wi.nAntialias`, `DrawTurtle2`, `gs.nDecaType == 6`'s heart
    decoration, the aspect-list scroll offset). **One is a real gap.**

    **`XChartRising()` draws without its altitude gradient on Qt, and a
    backend `#ifdef` is the only reason.** xcharts2.cpp:1682:

        if (!gi.fBmp || !gs.fColor || (gi.fFile && gs.ft > ftBmp)
        #ifndef WINANY
          || !gi.fFile
        #endif
          )
          n = (n << 1) | (alt >= 0.0);          // one bit per object
        else
          n = (n << 8) | (alt >= 0.0 ? 192+... : 64+...);   // a byte each

    The 8-bit branch accumulates one byte per object across up to three
    objects, which is a packed RGB -- which is why Windows hands `n`
    straight to `BmpSetXY()` when `gi.fBmp`. Probed on the Qt screen
    path: `gi.fBmp=1 gs.fColor=1 gi.fFile=0 gs.ft=0`, so **every other
    clause is false and the `#ifndef WINANY` line is the only thing
    forcing the one-bit path**. Qt's on-screen Rising chart is drawn in
    eight flat palette colours where Windows draws a gradient.

    The clause is not arbitrary: the non-Windows draw below it is
    `DrawColor(ki[n])`, and `ki` is `KI ki[8]` -- an eight-entry palette
    that a packed RGB would index far out of bounds. So it is load
    bearing *as written*. But `KvFromKi()` is
    `((ki) >= 0 ? rgbbmp[ki] : -(ki))`: a negative KI already carries a
    packed RGB, so `DrawColor(-(KI)n)` is the gradient path without any
    new machinery, and `gi.bmpRising` is declared and freed
    unconditionally already -- only `BmpCopyToWin()`'s `HDC` is
    Windows-specific.

    **Not fixed here, deliberately.** It is a visible rendering change to
    a chart, it wants a Windows side-by-side to say what the target
    looks like, and it is a port increment rather than the refactor T6
    asked for. Recorded with the mechanism and the one-line shape of the
    fix so the next session starts from the measurement.

    **Nets**: no code change; probe only. The measurement script is in
    the work log rather than a tool, since it answered its question once.

156. **The Rising chart draws its altitude gradient on Qt.** Item 155
    measured the gap and left it; this closes it. Two lines.

    `XChartRising()` packs one value per pixel column, either one bit per
    object -- an index into `KI ki[8]` -- or one byte per object, which
    across up to three objects is a packed RGB. The choice was:

        if (!gi.fBmp || !gs.fColor || (gi.fFile && gs.ft > ftBmp)
        #ifndef WINANY
          || !gi.fFile
        #endif
          )

    That last clause sent every non-Windows **screen** render down the
    one-bit path. Probed on Qt: `gi.fBmp=1 gs.fColor=1 gi.fFile=0
    gs.ft=0`, so it was the only reason. Windows drew a gradient there
    and so did every build's `-Xb` file render; only the Qt and X11
    screens did not.

    The clause was load bearing as written, because the draw it guards is
    `DrawColor(ki[n])` into an eight-entry palette that a packed RGB
    would index far outside. But `KvFromKi()` is
    `((ki) >= 0 ? rgbbmp[ki] : -(ki))` -- **a negative KI already carries
    a packed RGB**, which is how DrawPoint's own PostScript branch passes
    one (`DrawColor(-(int)gi.kvCur)`). So the guard comes off and the
    draw becomes `DrawColor(!gi.fBmp ? ki[n] : -(KI)n)`.

    Qt's `DrawPoint()` paints `gi.kvCur` exactly, so it gets the true
    colour. X11's `DrawColor()` maps a negative KI to the nearest palette
    entry (`ki = KiFromKv(-ki, fTrue)`), so X11 gets an approximation --
    which is what X11 does with every RGB value in this program, not a
    new compromise.

    **Measured, not eyeballed**: the Qt screen render goes from **9
    distinct colours to 80,595**, and its colour profile now matches the
    `-Xb` file render that was already correct -- black, white, the
    191-grey frame, then a long tail of gradient shades.
    **Falsified**: putting the clause back drops it to 16 and fails.

    New `rising-gradient` group. Getting it to survive the full suite took
    three tries and every one was the inherited-state trap this file's
    header warns about: it passed alone and drew **3** colours in the
    full run, then 9 after borrowing `gs.fColor` and clearing
    restrictions, and only passed once `gi.fBmp` was borrowed too. All
    three are genuine preconditions -- without a 24 bit target, eight
    palette colours is the *right* answer -- so the test states them
    rather than assuming them.

    **Nets**: suite 3547/0; chart matrix 0 of 6,936; switch matrix 0 of
    75,635; graphics matrix 0 of 224 -- the last one matters here,
    because it renders to *files*, where the clause never applied: it
    proves the file path did not move while the screen path did.
    `tools/win-tests.sh` 2 scenarios; four builds, zero warnings each.

    One process note. Undoing a bad edit here, I reached for
    `git checkout qttest.cpp`, which CLAUDE.md forbids by name -- it
    reverts the file's whole share of the change, not the one line. It
    cost nothing this time because everything else in that file was
    committed, which is luck rather than method. And the edit that needed
    undoing was `int cColor = 0;` colliding with `#define cColor 16` in
    astrolog.h: the `c` prefix means "count of" and the namespace is
    already occupied, which CONVENTIONS.md says and I did not check.

157. **Item 153's correction was itself wrong, and the real answer is
    seven house systems, not two.** This retracts a claim made and
    committed earlier the same day, which matters more than the finding.

    Item 153 reported that item 141's two parked findings -- Topocentric
    wrapping beyond the polar circle, Pullen (S.Delta) with zero-width
    houses -- **do not reproduce**, and that *Sunshine* misbehaves
    instead. That measurement swept 40 systems and 8 latitudes on the
    Swiss engine at **one date and one longitude**. Item 141 was right
    and item 153 was wrong: with four dates and longitudes the original
    findings reproduce immediately, on both engines.

    The lesson is the project's own rule turned on its author. "Verify a
    diagnosis before acting on it" was followed; the sample was not
    questioned. A single date is not a test of a claim about polar
    behaviour, because several house constructions key off the Sun's
    declination and December is where they break.

    **What the full sweep says** -- 40 systems x 8 latitudes x 4
    date/longitude cases x both engines, 99 bad combinations:

        system            failure       engines
        Pullen (S.Delta)  zero-width    Matrix + Swiss
        Topocentric       cusps wrap    Matrix + Swiss
        Sunshine          zero-width    Swiss
        Campanus          cusps wrap    Matrix + Swiss
        Regiomontanus     cusps wrap    Matrix + Swiss
        APC               cusps wrap    Swiss
        Savard-A          cusps wrap    Swiss

    "Cusps wrap" means the twelve gaps sum to 1080, 1800, 3240 or 3960
    degrees instead of 360 -- the cusps go round the circle three to
    eleven times, so house assignment is meaningless rather than merely
    odd. `ComputeHouses()` (calc.cpp:508) guards **two** systems,
    Placidus and Koch, falling back to Porphyry with a warning; Swiss
    substitutes for its own Placidus and Koch. Nothing covers the other
    seven, and Pullen S.Delta fails at 66N -- *below* the polar circle
    the existing guard keys on (`rDegQuad - is.OB`).

    **Oracle leg 4b** now sweeps the invariant across latitude and
    engine, which is what leg 4 could not see running at one mid latitude
    on whichever engine was configured. It asserts the partition holds
    for every combination outside the seven, and that all seven still
    fail -- so a system that starts degenerating shows up, and one that
    gets fixed shows up too. **Falsified both ways**: dropping
    Pullen S.Delta from the set reports it as a new degeneracy; adding
    Porphyry, which is healthy, fails the "still unfixed" half.

    The set is pinned **without latitude thresholds**, and that is the
    second lesson from the same mistake: a first version stored the
    lowest failing latitude per system, taken from one sample, and the
    oracle's own grid contradicted it within the hour. The latitude moves
    with the date; the set does not.

    **Not fixed, and this one really is a maintainer decision.** Widening
    the guard changes house math for seven systems in both builds, and
    the two engines would need it in different places -- `ComputeHouses()`
    for the Matrix path, and something after `swe_houses_armc_ex2()` for
    the Swiss one, since five of the seven fail there. The evidence is
    above; the shape of the fix is the existing Placidus/Koch guard,
    widened.

    **Nets**: suite 3551/0; oracle 322 assertions, 187ms for the added
    sweep. Test-only change.

158. **Five house systems stop returning cusps that circle the zodiac
    three to eleven times.** Item 157 measured seven systems degenerating
    toward the pole and left the fix as a maintainer decision. Taken.

    **Reading them separated two classes that the measurement had
    lumped together.** Pullen (S.Delta) and Sunshine produce a
    *zero-width house* -- two cusps on the same degree -- but their gaps
    still sum to 360, so the circle is still covered exactly once.

    For Pullen the collapse is demonstrably deliberate:
    `HousePullenSinusoidalDelta()` says
    `chouse[sAqu] = chouse[sPis] = Midpoint(...)` when a quadrant is
    under 30 degrees wide, because three houses will not fit in it. That
    is its author's degenerate case, not a broken partition.

    **For Sunshine that is an assumption, and it is flagged rather than
    claimed.** Sunshine is `ch = 'I'` handed to
    `swe_houses_armc_ex2()` -- the behaviour is inside the Swiss
    Ephemeris library, which this project treats as third-party
    (REFACTORING.md non-goals). All that is established is that its gaps
    sum to 360, which is why the guard leaves it alone; whether the
    zero-width house is intended by the library or is a defect in it is
    **not** established here. Checking that means reading `swehouse.c`
    and is open.

    The other five -- **Topocentric, Campanus, Regiomontanus** on both
    engines, **APC** and **Savard-A** on the Swiss one -- return cusps
    whose gaps sum to **1080, 1800, 3240 or 3960** degrees. Those do not
    partition anything.

    **The fix is a post-condition, not a list.** `FEnsureHousePartition()`
    (calc.cpp) sums the twelve gaps and, if they do not come to 360,
    prints the warning the Placidus/Koch guard already prints and falls
    back to Porphyry. It is called at the end of `ComputeHouses()` and at
    the end of `SwissHouse()`'s own branch, so **one check serves both
    engines** -- which matters because five of the seven fail on the
    Swiss path, where the existing guard does not reach. It also catches
    a system that starts failing without anyone adding it to a list.

    Checking the *sum* and not the minimum gap is what keeps the two
    deliberate collapses working. That distinction is the whole reason to
    read the code rather than act on the measurement.

    **What a user sees.** Longyearbyen (78N13), December, Topocentric:

        before   <9>22Can17  <10>13Can39  <11> 0Can54  <12> 4Tau09
        after    <9> 4Sag24  <10>13Cap39  <11> 4Aqu24  <12>25Aqu09

    Three consecutive cusps inside 22 degrees of Cancer, then a jump to
    Taurus, become three clean ~30-degree steps. The Ascendant
    (`15Pis54`) is identical either way -- it was only the intermediate
    cusps that were meaningless -- which is why this survived so long.

    **Falsified**: removing both calls puts 26 combinations back into
    oracle leg 4b's "NEW polar degeneracy" list and fails it. The leg's
    expected-degenerate set is two now, not seven.

    **Nets**: suite 3551/0; chart matrix 0 of 6,936; switch matrix 0 of
    75,635; graphics matrix 0 of 224 -- all three pinned at Seattle, so
    they prove the change is confined to the latitudes it targets rather
    than proving it works; oracle leg 4b is what proves that. Settings
    round trip; five audits; `tools/win-tests.sh` 2 scenarios; four
    builds, zero warnings each.

    One process note, caught by the project's own rule. The inserted
    block went in with bare `\n` and left 43 LF-only lines in a CRLF
    file. The `CR == LF` assertion is what caught it, exactly as
    CLAUDE.md says it would -- "assert CR == LF after every scripted
    write" is not advice, it is the only thing standing between a
    scripted edit and a file that diffs as entirely rewritten.

159. **The source becomes LF, four categories stay as they ship, and a
    conversion sweep corrupted 28 binaries on the way.** The maintainer
    asked whether anything still needs CRLF in 2026. Measured: no, not in
    the source.

    **The evidence.** Converting every CRLF file and rebuilding left
    **all 64 object files byte-identical** -- 31 from g++ 11 on Linux, 33
    from mingw g++ 10 for Windows -- which is P7's standard for a change
    that must not touch generated code. `windres` produces the same
    `.res` from either input. The tree was a 55/53 split, upstream's
    files CRLF and this fork's own LF, held together by a per-file rule
    that had been broken four times before being caught and twice more
    since (items 145 and 158, the second one this same day).

    **Four categories are deliberately not converted**, each listed in
    `.gitattributes` with its reason:

    - **Binaries** -- `.se1`, `.ttf`, `.pdf`, `.docx`, `.rtf`, images.
    - **Files Windows or VMS tooling owns** -- `.sln`, `.vcproj`,
      `.vcxproj`, `.rc` (Visual Studio's resource editor), `.def` (the
      Windows linker), `.url` (Explorer's Internet Shortcut format),
      `makefile.com` (a VMS DCL procedure).
    - **`font/`**, a third-party distribution, table and OFL FAQ included.
    - **The data files the program parses**, `.as` and `.csv`. **This one
      is not cosmetic.** With `astrolog.as`, `atlas.as`, `timezone.as`
      and `nrvate.as` converted, `tools/switch-matrix.sh` moved six lines:
      `-0q` went from "Assuming first century C.E. is really meant
      instead of 1908 / Couldn't find anything in atlas matching
      location" to "Value 0 out of range from 1 to 12" twice. Something
      in the data parsers reads a CR as content. Not diagnosed here; the
      files stay as they ship and the diff is the evidence.

    **And the sweep corrupted 28 binary files.** The conversion loop
    excluded three extensions -- `.png`, `.ico`, `.bmp` -- so it stripped
    `\r` bytes out of every `.se1` ephemeris, every `.ttf`, the `.pdf`s
    and the `.docx`s. `earth.bmp` survived because it happened to be on
    the list; nothing else did. It was caught by the maintainer reading
    `git status`, and independently by a parallel session running the
    suite against `./ephem` and seeing "Ephemeris file
    ./ephem/sepl_18.se1 is damaged (0)". All 28 were restored from HEAD
    and verified byte-identical.

    Two things follow, and the second is the one that matters.
    An exclusion list written from memory is not an exclusion list.
    And **the guard has to be the thing that cannot be forgotten**:
    `.gitattributes` now marks the whole tree `-text`, so a clone on
    Windows with the default `core.autocrlf=true` cannot rewrite
    anything, and `tools/line_endings_audit.py` is the seventh standing
    audit. Its failure message says to add a file to the skip list rather
    than to strip its CRs, because the message it *used* to print --
    "Strip them: tr -d '\r' < FILE" -- was, for a few minutes, precise
    instructions for repeating the corruption.

    **Nets**: 64 objects byte-identical; four builds clean with zero
    warnings; suite 3551/0; chart matrix 0 of 6,936; switch matrix 0 of
    75,635 *after* the data files were put back, which is how the parser
    sensitivity was found; graphics matrix 0 of 224; seven audits and
    three generated tables; settings round trip; `tools/win-tests.sh`.

    Two generators needed a one-line fix: `rc_accel.py` and `rc_cmd.py`
    split the resource on a literal `'\r\n'`, which found nothing the
    moment anything was LF. They use `splitlines()` now and take either.

160. **Sunshine's zero-width house is a guard the library wrote and then
    commented out.** Item 158 left two systems alone because their gaps
    still sum to 360, and justified it by reading
    `HousePullenSinusoidalDelta()`. That argument covers Pullen
    (S.Delta); item 158's own correction noted it was extended to
    Sunshine by analogy, without evidence. Read properly, the analogy is
    wrong.

    Sunshine is `ch = 'I'` handed to `swe_houses_armc_ex2()`, so the
    behaviour is in `swehouse.cpp`. That file falls back to Porphyry
    inside the polar circle for **Placidus** (line 1831), **Koch** (1251)
    and **Regiomontanus** (1628), each with the same message: "within
    polar circle, switched to Porphyry". Its Sunshine dispatch has that
    fallback ready too, at line 1175 --

        if (retc == ERR) {  // only Makransky version does this
          strcpy(hsp->serr, "within polar circle, switched to Porphyry");

    -- and the check that would return ERR is **commented out in both
    Sunshine solutions**, Makransky at 2919 and Treindl at 3055:

        // if (90 - fabs(lat) <= ecl) {
        //   strcpy(hsp->serr, "Sunshine in polar circle not allowed");
        //   return ERR;
        // }

    Astrolog asks for `'I'`, the Treindl solution. So it receives cusps
    that the library's own author wrote a guard to refuse, in a file that
    refuses them for every comparable system. That is not a deliberate
    degenerate case; it is a disabled one.

    **Fixed at the boundary, not in the library.** Third-party code is
    out of scope (REFACTORING.md non-goals) and editing `swehouse.cpp`
    would be a merge liability; the check Astrolog already performs on
    the result is in scope. `FEnsureHousePartition()` now treats a
    zero-width house as degenerate as well as a non-closing circle, and
    produces exactly the Porphyry fallback that file uses everywhere
    else. The three systems built by `HousePullenSinusoidalDelta()`
    (`hsSineDelta`, `hsSineDeltaEP`, `hsSineDeltaVtx`) are exempt,
    because that collapse **is** deliberate and is Astrolog's own.

    **The two thresholds had to be made to agree, and finding that out
    cost a wrong first attempt.** The guard first tested `rGapMin > 0.0`
    while the oracle's sweep called anything under 0.001 degrees
    degenerate. Sunshine's narrowest house is a rounding error rather
    than an exact zero, so the guard passed the chart and the oracle
    failed it -- the fix appeared not to work. Both use 0.001 now (3.6
    arcseconds), with a comment at the guard saying why, because two
    checks that disagree about what "degenerate" means will disagree
    again.

    Longyearbyen, June, Sunshine: cusps 9/10/11 were `25Tau59`,
    `13Can18`, `22Leo51` -- 48 and 39 degree steps around a collapsed
    pair -- and are now `10Gem34`, `13Can18`, `10Leo34`, with the warning
    printed. **Falsified**: dropping the zero-width half of the condition
    puts Sunshine back in oracle leg 4b's new-degeneracy list at 70N,
    75N and 82N.

    Leg 4b's expected-degenerate set is one system now: Pullen (S.Delta),
    whose collapse its author wrote.

    **Nets**: suite 3551/0; chart, switch and graphics matrices all 0 --
    they are pinned at Seattle, so they prove containment and leg 4b
    proves the fix; settings round trip; audits; `tools/win-tests.sh`;
    four builds, zero warnings each.

161. **A return really returns, and the chart matrix learns about the
    pole.** Item 11's next oracle invariant, plus the coverage gap the
    house work exposed.

    **Leg 9: the return.** Astrolog has no "cast the return chart"
    command -- `-tr` searches a month for the moments a transiting object
    conjoins its own natal position, and `-5` (`us.fListAuto`) appends
    each hit to the chart list. That makes the invariant checkable end to
    end rather than by reimplementing the search: restrict both charts to
    the Sun, run `ChartTransitSearch()` over the month a year after the
    natal one, then cast every moment it reported and require the Sun to
    be back where it started. Nothing else in the suite calls that
    function at all.

    **Falsified** where it counts -- in the search, not in the check.
    Adding `ciEvent.tim += 24.0` before `FAppendCIList()` makes the
    reported moment a day late, and the leg fails with the Sun one degree
    off. A test that only perturbed its own comparison would have proven
    nothing.

    **And the matrices could not have caught any of the house work.** A
    parallel session pointed out that items 157, 158 and 160 changed
    house math across six systems, 205 lines in four files, and moved
    **nothing** in 71 text charts or 224 renders. That is consistent --
    the fixes bite toward the pole and both matrices pin Seattle and
    Chicago -- but it means the differentials were blind to exactly the
    code being changed, and a regression there would have diffed to zero.

    `tools/chart-matrix.sh` now ends with six house systems cast at
    **Longyearbyen, 78N13, in December**: the latitude where the
    constructions degenerate, the month when the sun-declination ones do,
    and a city in the shipped atlas. **Proven to have teeth**: against a
    binary from before the guards it moves 166 lines, and all of them are
    inside the new section -- the 6,936 lines above it are byte-identical,
    so the case adds coverage without disturbing any.

    **Nets**: suite 3553/0; switch and graphics matrices 0 against HEAD;
    chart matrix 0 against HEAD and 166 against the pre-guard binary,
    which is the point; settings round trip; line endings; four builds,
    zero warnings each.

162. **Half of T4's gap closes, and five settings turn out to have been
    untested.** Item 140's bug -- the whole `-b` backend family silently
    dropped by the settings writer for five days -- fell through a hole
    that neither standing check could see. `registry_audit.py` verifies
    every spelling the program *writes* resolves to a row, not that every
    setting *gets* written; and the round trip could not see it because
    the fixture never set those fields.

    A **static** audit in the other direction was measured and rejected
    on 2026-09-01 (recorded at T4): of the 120 settings fields
    `switch.cpp` assigns, 69 are never named in `FOutputSettings()`, but
    most are false positives because the writer packs values -- one
    `:YXG %06d` line carries six `gs.nGlyph*` fields without naming any.
    A script would have to learn the writer's encoding.

    **The behavioural version needs no encoding: put the switch in the
    fixture and let leg 3 prove it round-trips.** So the question is only
    which switches are in it. Measured: of `rgswranged[]`'s 71 rows, 54
    are AstroExpression hooks that `-od` structurally cannot persist
    (QT_TESTING.md says so), leaving 17 -- and **five of those had no
    fixture line**: `-YAa` (aspect angles), `-YjC` (house influences),
    and `-Yk0`, `-Yk7`, `-Yk` (the rainbow, ray and main colour tables).
    Leg 3 was proving nothing about any of them.

    All five now have one, and the fixture is 36 sentinels.
    `tools/fixture_coverage_audit.py` is the eighth standing audit and
    fails if a row loses its line. **Falsified**: deleting the `-Yk0`
    line fails it naming that switch.

    One of the five nearly read as a second item-140. `-YAa` looked
    unsaved -- nothing in io.cpp emits that spelling -- but the writer
    stores aspect angles as `-Aa 5 66.6`, a different spelling, and it
    round-trips correctly. The fixture's EXPECT pattern names the *saved*
    spelling rather than the switch, which is why it can express that at
    all, and the audit's header says so: a missing line is not evidence
    of a missing save.

    **What is still open, measured rather than waved at.**
    `rgswitchdef[]` has 191 rows, 41 declaring `carg>0`, of which 31 are
    absent from the fixture. Most are imperative rather than settings --
    `-x` casts a harmonic chart, `-XI` loads a background bitmap, `-YYt`
    prints formatted text -- so "takes an argument" is not the same
    question as "is a saved setting", and separating them needs a
    judgement per row rather than a filter. That is the next increment,
    and item 140's own `-b` family lives there.

    **Nets**: settings round trip all three legs, 36 sentinels; eight
    audits; suite 3553/0.

163. **The graphics matrix counted its own failures and threw the count
    away.** A parallel session running it in an isolated checkout got
    `rc=0` with 115 of 224 renders missing. The tool is item 148's, and
    its header says, in its own words, that "a render that produces NO
    file prints MISSING and the run's tail counts them" because "a
    harness whose invocations all fail diffs to zero and reads exactly
    like a proof". It counted them. It printed them. Then it ended with

        echo "== $runs renders, $missing produced no file"

    and nothing set an exit status from `$missing`.

    **Reproduced here at full strength**: pointing `GRAPHICS_MATRIX_CFG`
    at a malformed settings file makes **all 224 renders fail**, and the
    old script reported that as success. Two such runs diff to zero
    against each other, which is the exact shape the header warns about,
    written by someone who had just written the warning.

    Fixed: nonzero exit when any render produced no file, with a message
    naming the two causes worth checking first (a config that does not
    resolve the ephemeris, and a binary at a deep path). **Falsified both
    ways**: the malformed config now exits 1 naming 224 of 224, a normal
    run exits 0.

    **One claim from that report did not reproduce and is not written
    down as fact.** The 115 failures were attributed to running against
    the bundled `ephem/` rather than `/swe`. Pointing `-Yi1`, `-Yi2` and
    `-Yi3` at `./ephem` here gives **0 of 224 missing**, so whatever
    caused it is environmental rather than a property of the bundled
    ephemeris, and the header says only that the config must resolve the
    ephemeris. The exit status is what makes the difference visible
    either way, which is the point: the fix does not depend on knowing
    why a render failed.

    **Nets**: the harness against itself, both directions; suite 3553/0;
    unchanged behaviour otherwise -- this is a status line, not a render.

164. **The suite's intermittent was nine hard-coded temp filenames, and
    two sessions running it at once.** Work log item 141 recorded a
    "(0 bytes)" failure in the `long-strings` group -- "two failures in
    about sixteen runs, not reproduced, not diagnosed, not claimed
    fixed". A parallel session hit it hard enough today to reproduce it 7
    times in 8 at `627480a` and 5 in 8 at `28599d4`, so it was neither
    new nor rare; it had simply never been cornered.

    **The clue was that it passes alone.** `long-strings` on its own:
    clean every time, 5 of 5 and later 4 of 4. In the full suite: about
    half. That reads as an inter-test interaction, and two wrong guesses
    followed -- the group's own nested `Action()` calls not restoring
    `is.S` (fixed anyway as hygiene, and measured as making no
    difference: 2 of 5 became 4 of 6), and `menu-actions` leaving state,
    which does reproduce it but only because it makes the run longer.

    **It is not an interaction between tests. It is an interaction
    between PROCESSES.** Every temporary path in qttest.cpp was a fixed
    name -- `/tmp/astrolog-qt-longstrings.txt` and eight siblings -- and
    the loop does `remove(szOut); Action();` per chart mode. Two suites
    running at once share all nine. One process removes the file the
    other is about to measure, and the measurement is exactly the
    assertion's subject: `cb > 0`.

    **Measured, and it is not subtle.** Four solo runs: clean, clean,
    clean, clean. Four *pairs* run concurrently: every one of the eight
    processes failed. Across those eight runs, **70 assertion failures**.
    With the nine names carrying `getpid()`: the same four concurrent
    pairs produce **0**.

    That also explains the history. Item 141's 2-in-16 was a day with one
    session running; today's 5-in-8 and 7-in-8 were measured while two
    sessions were both running suites, and neither of us thought to say
    so. **A rate that depends on who else is on the machine is not a
    rate**, and both of us quoted ours as though it were a property of a
    commit.

    **What is NOT fixed, and is a different bug.** After the temp names
    were made unique, six solo runs gave five clean and one `rc=134`,
    and the concurrent set produced two `rc=134` and one `rc=139`
    alongside its zero assertion failures. Every one of those crashes
    ends on the same line: `free(): invalid pointer` or a segfault
    immediately after `PrintProgress("Writing wireframe to file.")`.
    So the wireframe writer has a memory bug of its own, roughly 1 run in
    6, entirely independent of the temp files. `WriteWire()` walks
    `gi.bm` -- the bitmap allocation reused as wireframe scratch -- from
    its start to `gi.pwWireCur`, and `WireNum()`'s bound check tests the
    offset *before* writing a 2-byte `word`. That is where to start; it
    is not diagnosed here and this item does not claim it is.

    **Nets**: 70 assertion failures across 8 concurrent runs before, 0
    after; solo runs unaffected; suite 3553/0 when it does not hit the
    wireframe crash.

165. **The abort was mine, from six hours earlier, and it is the exact
    incident CLAUDE.md records.** Item 164 fixed the suite's "(0 bytes)"
    failures and left a separate crash: `free(): invalid pointer` or a
    segfault, about one full-suite run in six. It localised that crash to
    the wireframe writer, because every crashing run's last line was
    `Writing wireframe to file.` **That localisation was wrong**, and
    wrong for a reason this same session had already diagnosed and then
    failed to apply: `PrintProgress()` writes to **stderr**, which is
    unbuffered, while the suite's own output is block-buffered when
    redirected. The last line before a crash is the last *unbuffered*
    line, not the last thing that happened.

    One `gdb` run settled it:

        #9  _IO_free_backup_area (fp=0x5555562f7910)
        #10 _IO_new_file_overflow
        #11 __GI__IO_putc
        #12 PrintSz(char const*)
        #13 ChartTransitSearch(int)

    `ChartTransitSearch()` is called from **oracle leg 9**, added the
    same day in work log item 161. It prints its results through
    `PrintSz()`, which writes to `is.S` -- and `is.S` is only ever opened
    by `Action()`. The GUI runs inside one, so `putc()`ing into that
    stream from a test made glibc free a backup area it never allocated.

    CLAUDE.md states this verbatim, under "Working method": *"A
    regression test can be the regression. A new test called a print
    routine outside `Action()`, so it wrote to a `FILE *` nothing had
    opened; glibc freed a buffer it never allocated and the suite began
    aborting six runs in twelve."* That is work log item 145, and leg 9
    reproduced it on a day the same lesson was quoted twice in commit
    messages.

    Fixed by giving the search a stream of its own -- a temp file, with
    `is.S` saved and restored around the call, the pattern
    `CaptureTextToFileQt()` uses. **10 solo runs clean where it was about
    1 in 6, and two concurrent pairs clean.** The `gdb` backtrace is the
    proof of mechanism; the run counts are the proof of fix.

    **Two corrections owed.** The wireframe writer is exonerated: item
    164's "WriteWire walks gi.bm ... that is where to start" sent a
    parallel session to analyse code that was never involved, and it
    produced a careful reading of an unchecked read there that may still
    be worth fixing on its own merits but is not this crash. And item
    164's "roughly 1 run in 6, independent of the temp files" was right
    about the independence and wrong about everything else.

    The general lesson is not "read backtraces" -- it is that **a
    diagnosis from output ordering is worthless when stderr and stdout
    have different buffering**, and this session identified exactly that
    hazard hours before relying on the ordering anyway.

    **Nets**: suite 3553/0; 10 solo runs and 2 concurrent pairs with no
    crash; four builds clean.

166. **Two buffer guards reported the overflow and then performed it.**
    Chasing item 165's crash put two sessions in `xdevice.cpp`'s
    wireframe code, and it turned out not to be the crash at all. Reading
    it anyway found something else -- which is a hazard as much as a
    result, since "we were looking there anyway" is how a coincidence
    gets promoted into a cause. This one stands on the code, not on any
    incident.

    `WireNum()` and `WriteMetaWord()` both do:

        if (offset >= limit) {
          PrintError("... would be more than %ld bytes.");
          Terminate(tcFatal);
        }
        *cursor = value;    // <-- reached anyway
        cursor++;

    **`Terminate()` returns** when `us.fNoQuit` is set -- its first two
    lines are `if (us.fNoQuit) return;` (general.cpp:3377) -- and
    `us.fNoQuit` is `-0q`, a documented switch that
    `tools/switch-matrix.sh` exercises. So under `-0q` each guard prints
    its message and then writes one word past the end of `gi.bm`, and
    keeps doing so for the rest of the drawing. Both fixed with a
    `return`, which is what the message already claims happens.

    **No reproduction, and that is stated rather than glossed.** At the
    default `gi.cbWire` of 8 MB the wireframe cannot get there: `gs.xWin`
    and `gs.yWin` are clamped to `BITMAPX`/`BITMAPY` (xscreen.cpp:1573),
    and pushing the canvas and object count as far as the clamp allows
    plateaus at 8.88 MB of *output text*, roughly 5 MB of buffer. The
    path is reachable when the allocation itself came back smaller --
    `gi.cbWire` starts at MAXMETA and is reduced by MAXMETA/8 per failed
    attempt -- so it wants memory pressure as well as `-0q`. Narrow, but
    the code is wrong regardless of how narrow.

    **Two other findings in the same function are recorded and NOT acted
    on**, both from the parallel session, both correct and neither a
    cause of anything observed:

    - `WireNum()`'s guard tests the offset before writing a 2-byte word,
      so the arithmetic hole is at `offset == cbWire-1`. Unreachable:
      the cursor starts at offset 0 and advances by one word, so every
      offset is even, and `cbWire` is `MAXMETA` decremented by
      `MAXMETA/8`, so it is even too. An even offset never equals an odd
      `cbWire-1`.
    - `WriteWire()`'s loop is `while (pw < gi.pwWireCur)` with no check
      that a whole record remains, and the segment branch reads
      `pw[0]`..`pw[5]`. A short tail would read up to five words past the
      cursor. Not reachable today either, because the writer only ever
      appends complete records -- and the one thing that could have left
      a partial record is the fall-through fixed above.

    That last point is the reason to fix the fall-through even without a
    reproduction: it was the only way to create the partial record the
    over-read needs.

    **Nets**: suite 3553/0; chart, switch and graphics matrices all 0;
    `tools/win-tests.sh`; four builds, zero warnings each. Behaviour is
    unchanged wherever the buffer does not overflow, which is everywhere
    the matrices reach.

167. **The build system, asked what needed work: seven answers, and the
    two that mattered could hand you a binary that did not match its
    source.**

    It started as a smaller question -- "why does `make` not build
    `astrolog-qt` by default?" -- and the honest answer is that `Makefile`
    is upstream's and builds upstream's binary, while this fork's three Qt
    makefiles are its own. Ten scripts here expect a plain `make` to leave
    `./astrolog` behind, so the default target cannot simply move. Named
    targets instead (`make qt`, `make qt-test`, `make qt-asan`, `make win`,
    `make all`), each exactly the command CLAUDE.md already documented.
    That was `87a5390`. Everything below came out of looking properly
    afterwards.

    **A parallel build could compile into a directory that did not exist
    yet.** `make all -j4` failed on a fresh tree with

        Fatal error: can't create obj-qt-test/sweph.o: No such file
        or directory

    reported by the maintainer, not by anything here. All three Qt
    makefiles listed `$(OBJDIR)` among `$(NAME)`'s *ordinary*
    prerequisites, which does not order it before the objects -- under
    `-j` make is free to start a compile before the `mkdir`. `Makefile.win`
    had the order-only form (`| $(ODIR)`) all along; the Qt three now do
    too. The new `all` target did not create this race, it just made the
    tree cold often enough to hit it (`743817c`).

    **`make clean` cleaned a third of the tree.** It is upstream's target,
    and upstream never knew about three Qt binaries and their object
    directories. Now it removes what the tree can build, which is the
    conventional expectation; `clean-console` keeps upstream's narrower
    behaviour because `tools/asan-sweep.sh` needs exactly it, and the
    comment there says why (`47baa42`). **This has a sharp edge worth
    stating:** it deletes all four binaries, so a second session sharing
    the tree loses them mid-run. That happened here within the hour --
    five checks appeared to fail at once, and the cause was the other
    session's `clean`.

    **A header change rebuilt nothing.** All five makefiles tracked
    exactly two headers by hand:

        $(OBJS): astrolog.h extern.h

    That line *was* the dependency graph -- itself a fix, from `01f52b1`,
    for the fork's own two headers. Everything else a source includes was
    invisible. Measured by touching a header and counting what `make -n`
    would then compile:

        placalc.h    0 objects        swephexp.h   0
        sweodef.h    0                (every build, all five makefiles)

    Zero. This is the stale-binary class CLAUDE.md warns about -- "if a
    check claims to have rebuilt something, look at the binary's
    timestamp" -- except that nothing here was even claiming. Editing a
    Swiss Ephemeris header and re-running the suite would have tested the
    *old* objects and reported 3553/0.

    Fixed with `-MMD -MP` plus `-include $(OBJS:.o=.d)` in all five: the
    compiler writes each object's real dependency list as it compiles it,
    and make reads it back next time. The hand-written line is deleted.
    Counted from the generated `.d` files afterwards, in the console
    build: `placalc.h` reaches 2 objects, `swephexp.h` and `sweodef.h` 9
    each, and `astrolog.h` and `extern.h` all 31 -- which is what the
    hand-written line had right, and the whole of what it had right.

    **The console makefile named no C++ standard.** `calc.cpp` uses class
    template argument deduction (`Borrow bciCore(ciCore);`), a C++17
    feature; g++ 11 defaults to `gnu++17`, so it compiled by accident.
    `Makefile.win` relied on the same accident and mingw g++ 10 defaults
    to `gnu++14` -- which is how the Windows build went 62 commits without
    compiling (item 146). Saying it out loud is the fix for the class, not
    just the instance, so `-std=gnu++17` is now explicit in both, with
    that reasoning at the flag (`20ffda7`).

    **`Qt5Test`/`Qt6Test` was linked into two builds and used by neither.**
    Nothing in the tree includes `<QtTest>`; the five "QtTest" hits are
    this project's own `NRunQtTestsQt`/`NRunQtTestTableQt`, which are not
    Qt Test at all. Dropped from `Makefile.qt.test` and `Makefile.qt.asan`.

    **And the source list was written out five times.** Adding one source
    file meant editing five makefiles, in five different notations, with
    nothing in the tree able to notice a missed one except a link error in
    whichever build happened to be compiled last. Item 96 records the day
    that bit: "five makefiles gained the object." `Makefile.srcs` now holds
    it once, in five groups -- core, graphics, Swiss, and one per backend
    -- and each makefile derives its own object paths with `patsubst`.

    **The groups are not decoration: they preserve each build's original
    link order**, which is what makes the change provable. After a full
    clean rebuild, `astrolog`, `astrolog-qt`, `astrolog-qt-test` and
    `astrolog-qt-asan` are **byte-for-byte identical** to the binaries the
    hand-written lists produced, and stayed identical through a second
    rebuild triggered by the header touches above.

    The Windows build is the one exception, said plainly: it had listed
    the same files alphabetically, with `wdialog`/`wdriver` in the middle,
    and no grouping can spell both that order and the Linux one without
    naming every file twice. Its objects are ordered by these groups now.
    Same set, different layout -- 335,919 bytes of `astrolog.exe` move.
    What was checked instead: the same 6,563 defined symbols, the same
    file size to the byte, the eight captured text charts identical
    between an old-order and a new-order link (866 lines,
    `tools/text-chart-capture.sh` run twice), and `tools/win-tests.sh`
    passing both scenarios.

    **A finding worth keeping from that check: `astrolog.exe` *is*
    byte-reproducible.** Two links of identical objects differ in exactly
    five bytes, at offsets 137-138 and 217-219 -- all of them inside the
    PE header, where `objdump -p` reports a different Time/Date. So
    "5 bytes differ" is
    available as a proof for future Windows work, the same way byte
    identity is on Linux, and anything larger than 5 means the code
    actually changed.

    **And when Qt is not installed, the build gave the wrong advice and
    then tried anyway.** Measured rather than assumed, with
    `PKG_CONFIG_LIBDIR` pointed at nothing: pkg-config prints three lines
    of its own per missing module -- "add the directory containing
    `Qt5Widgets.pc' to PKG_CONFIG_PATH", which is the wrong fix on a
    machine that has simply not installed the package -- and then make
    compiles anyway with `QT_CFLAGS` empty, so what actually reaches the
    user is

        qtdriver.cpp:39:10: fatal error: QtCore/qglobal.h: No such file

    twenty lines below the cause, and interleaved with thirty others
    under `-j4`. The three Qt makefiles now stop before anything
    compiles, on one line, naming the modules pkg-config could not find
    and the package that supplies them -- `qtbase5-dev`, or
    `qt6-base-dev` when the Qt6 branch is selected. `clean` alone is
    exempt, because removing object files must not require Qt and the
    top-level `make clean` delegates to all three.

    Falsified four ways, a guard that cannot fire being decoration: with
    no Qt visible at all, each of the three stops with exit 2 and
    compiles nothing; with a pkg-config directory holding 387 `.pc` files
    and only `Qt5Network.pc` withheld, the message names exactly
    `Qt5Network`; `make clean` still succeeds with no Qt, dry-run
    included through the top-level delegation; and forcing `QT_MAJOR=6`
    names the Qt6 modules and `qt6-base-dev`. It changes no build
    command: `CPPFLAGS`, `LIBS` and `OBJS` expand identically to the
    commit before it in all three makefiles.

    **Nets**: full clean `make all` plus the Windows and ASan builds, zero
    warnings from any of them; the expanded object list of every makefile
    compared to its pre-change expansion, set-identical in all five and
    order-identical in four; four Linux binaries byte-identical; Windows
    as above. The Linux suite and the three matrices are not listed
    because they cannot say anything here -- the binaries they would run
    are the same bytes as the ones already tested. The pkg-config guard
    landed after the rest and repeated the clean rebuild, the byte
    comparison and the suite on its own.

168. **`make install`, with the data staying in the checkout.**

    There was no install target in any of the five makefiles -- measured,
    `grep -c '^install' Makefile*` returns zero for all five -- which is
    worth noting mainly because the maintainer's first message of the day
    was about the warnings "on make install".

    The design question was where the data goes: ephemeris files, atlas,
    fonts, `astrolog.as`, help text. The maintainer's answer was to leave
    it in the checkout, and that works cleanly, because of `FileOpen()`
    (io.cpp:73): **the first place Astrolog looks for any data file is
    the directory holding the executable**, taken from `argv[0]`
    (astrolog.cpp:850). So what gets installed is a two-line wrapper that
    runs the in-tree binary by absolute path, and file resolution is then
    exactly what `./astrolog` does.

    Measured from an unrelated working directory rather than assumed:

        <checkout>/astrolog -i nrvate.as        finds it
        a plain copy of the binary elsewhere    "File 'nrvate.as' not found."
        that copy with ASTROLOG=<checkout>      finds it

    So the wrapper needs neither an environment variable nor a recompile.
    The recompile is the part worth stating: the other way to do this is
    `-DDEFAULT_DIR=...`, which astrolog.h:184 exists precisely for, and
    it would make `make install` compile objects with different flags
    from the ones `make` produces, with nothing tracking the difference.
    That is the stale-object class item 167 had just finished closing, so
    it is not the way to install.

    Falsified by diffing the installed command, run from `/tmp` through
    `PATH`, against the in-tree binary: identical over a 116-line chart
    whose settings file is reachable *only* through the executable's
    directory -- `/tmp` has no `nrvate.as`, and a bare copy of the binary
    reports it missing. `DESTDIR` stages correctly, `uninstall` removes
    exactly what was installed, and a second `make install` is
    idempotent.

    A second check was thrown away rather than reported, which is the
    only reason to trust the first: a relative ephemeris path
    (`-Yi1 ephem`) also gave identical output between the two -- and
    identical output against `-Yi1 nosuchdir` as well, so that
    invocation never touched the path and proved nothing about it.

    **The trade, said plainly:** the installed commands depend on this
    checkout staying where it is. Move or delete the tree and they fail
    with "no such file", which is at least legible; re-run `make install`
    after moving it. `PREFIX ?= /usr/local`, so
    `make install PREFIX=$HOME/.local` needs no root, and `CMDS` selects
    which commands go in.

    Not done: a `.desktop` entry for the Qt build. The tree's only icons
    are four Windows `.ico` files, so it needs a converted PNG and a
    decision about where that lands. The command it would point at exists
    now either way.

169. **The Qt window had no icon at all, and now there is a menu entry to
    match.** The desktop entry was the ask; the missing icon was found on
    the way to it and is the more interesting half.

    Windows sets its window class icon from `astrlog1.ico` --
    `wndclass.hIcon = LoadIcon(wi.hinst, MAKEINTRESOURCE(icon))`
    (wdriver.cpp:572), where `icon` is the resource astrolog.rc lists
    first, with the comment that the lowest ID "ensures the application
    icon remains consistent on all systems". **This port called
    `setWindowIcon` nowhere.** It was invisible by inspection because
    qtdialog.cpp already loads `astrlog3.ico` for the dialogs, so the
    artwork looked wired up; and invisible in use because a window whose
    icon failed to load looks exactly like a window that never asked for
    one. Measured on a private display before the fix: no `_NET_WM_ICON`
    property at all.

    `IconAstrologQt()` (qtdriver.cpp) builds the icon from
    `icons/astrolog{16,32,48}.png` beside the executable, falling back to
    `astrlog1.ico`, in the two directories `PixAstrologIconQt()` and the
    bundled fonts already search. It is set on the *application*, not the
    window, so dialogs inherit it the way Windows' window class does.

    The PNGs are the same artwork: the three frames of `astrlog1.ico`
    extracted once, because `.ico` is not a format the icon theme spec
    expects. Worth recording that they are **not** needed for the window
    itself -- hiding `icons/` and leaving only the `.ico` still passes all
    eight assertions, so Qt's ICO reader does expose all three sizes.
    They exist for the desktop entry, which cannot use an `.ico`.

    **The menu entry.** `make install` now also writes
    `share/applications/astrolog.desktop` and the three PNGs into
    `share/icons/hicolor/NxN/apps/astrolog.png`. Two details are the
    whole difficulty: under `DESTDIR` staging the `Exec=` line and the
    icon name must be the **final** paths, not the staged ones (checked --
    a `DESTDIR` install writes `Exec=/usr/local/bin/astrolog-qt` while the
    file itself lands under the staging root); and `Categories` may name
    only one main category, which `desktop-file-validate` said out loud
    -- `Education;Science;` drew "application might appear more than once
    in the application menu", so it is `Science;Astronomy;` and the
    validator is silent.

    `QGuiApplication::setDesktopFileName("astrolog")` ties a running
    instance to that file. Measured, since the alternative was guessing:
    without it the main window's `WM_CLASS` reads `"astrolog-qt",
    "Astrolog-qt"` -- the executable's name, not the desktop file's -- and
    with it, `"astrolog", "astrolog"`.

    **Verified end to end rather than by inspection**, which is the only
    reason to believe any of it: `gtk-launch astrolog` on a private Xvfb
    display with `XDG_DATA_DIRS` pointed at the installed prefix brings up
    a window whose `WM_CLASS` is `astrolog` and whose `_NET_WM_ICON`
    xprop will draw for you. The new `app-icon` group (8 assertions) is
    falsified by hiding both icon sources: 8 of 8 fail, naming
    `pixmap(16) comes back 0x0`.

    *A method note, from a mistake made here.* Cleaning up that display,
    `pkill -f astrolog-qt` killed an unrelated Astrolog the maintainer had
    running. Kill the PID you started -- `xdotool getwindowpid` gives it
    -- and never a pattern; CLAUDE.md says the same thing about
    screenshots for the same reason.

170. **The Qt6 build had never had its warnings read, and the audit built
    to prevent exactly that covered the other four.** The maintainer ran
    `make qt6`, saw two `-Wdeprecated-declarations` lines go past, and
    asked why they had not been caught. Two answers, and the second one
    is worse.

    **The structural answer.** `tools/warning_audit.py` holds four builds
    against `tools/warnings.txt`: console, qt, qt-test, win. The Qt6
    build arrived at commit `ee0623e` and was never added, so from the
    day it existed it compiled outside every net this project has for
    compiler output -- the net whose own charter (item 146) is that
    nothing here had ever read a warning.

    **The behavioural answer.** Both lines appeared in *this session's*
    own build output, twice, hours apart. They were read, classified as
    "pre-existing Qt6-only deprecations, unrelated", and not mentioned.
    That is the same failure as not looking, with an extra step, and it
    is worth writing down because the audit alone would not have stopped
    it: an audit that does not cover a build produces silence, and
    silence is what a clean build looks like.

    **The warnings themselves.** `QMouseEvent::globalPos()` is deprecated
    in Qt6 in favour of `globalPosition()`, which returns a `QPointF` and
    does not exist in Qt5. Two call sites, both handing a screen position
    to `ShowContextMenu()`. `PtGlobalQt()` is the one spelling both
    accept, behind the `QT_VERSION_CHECK(6, 0, 0)` guard the tree already
    uses for `QAction`'s move to QtGui.

    **The ledger.** Qt6 gets its own file, `tools/warnings-qt6.txt`, and
    it is deliberately not folded into the main one: that ledger's first
    column names the set of builds agreeing on a site, so a fifth build
    would rewrite nearly every line -- and rewrite it back on any machine
    without a Qt6, which is most of them. The Qt6 leg is **skipped, not
    failed**, where no Qt6 is installed, so the audit stays a gate
    everywhere.

    It holds only what Qt6 warns about and Qt5 does not. The first
    generation produced 79 warnings in 73 sites, all of them shared-core
    lines already in the main ledger under three other builds; subtracting
    what the Qt5 build says leaves **zero**, which is the right resting
    state and makes any future Qt6-specific warning a single visible line.

    **Falsified**: putting one call site back to `globalPos()` makes
    `tools/warning_audit.py --file qtdriver.cpp` print

        qt6  qtdriver.cpp  mousePressEvent  -Wdeprecated-declarations  1
        'QPoint QMouseEvent::globalPos() const' is deprecated

    attributed to `qt6` alone, and reverse-patching it silences the audit
    again. `--file` covers Qt6 too now, so the seconds-long loop sees what
    the six-minute one does.

    **Nets**: both suites 3561/0 (Qt5 and Qt6); `make qt6` and `make qt`
    compile with zero warnings at their own flags; the four-build ledger
    unchanged at 316 in 100 sites, which is the check that this did not
    disturb it.

171. **The oracle reaches eclipses, the atlas and the interpretation
    tables -- and the last of those was a segfault reachable from the
    command line.** Plan item 11's remaining three surfaces, taken
    together because the third one stopped being a test-coverage job
    halfway through.

    **Leg 10, eclipses, is the strongest kind of check this project can
    make**: two independent implementations of the same astronomy, set
    against each other. Astrolog decides whether an eclipse is happening
    from its own 3D geometry -- `NCheckEclipseSolar()` walks `space[]`
    with real body diameters -- and never asks the Swiss library, which
    has its own eclipse finder. So `swe_sol_eclipse_when_glob()` and
    `swe_lun_eclipse_when()` are an outside answer, not the same code
    consulted twice.

    Three questions per eclipse, and the third is what a detector-only
    check would miss: does Astrolog see one at the moment the library
    names, does it call it the same kind, and does it see **nothing**
    halfway between two consecutive ones. Over five epochs from 1900 to
    2060: 60 solar eclipses, 55 midpoints, 40 lunar. **Every one agrees
    except a single case**, and it is the boundary rather than an error
    -- the total lunar eclipse of 2021-05-26 was total for about fifteen
    minutes at magnitude 1.009, Astrolog measures 98.9% umbral overlap
    and calls it partial. Named by date in the test, because a threshold
    there would hide the next real one.

    **Leg 11, the atlas, caught its own author twice.** The first draft
    probed eight world cities and passed -- while resolving Chicago's
    coordinates to Korla, in Xinjiang. Astrolog's longitude is positive
    *west*, and both sides of the comparison used the same wrong sign, so
    they agreed with each other about the wrong city. Adding "and the
    answer is within a degree of the city those coordinates belong to"
    is what made the convention itself part of the check.

    Then the invariant turned out to be wrong about the code: the search
    truncates each distance to a whole unit before comparing
    (`nDist = (int)rDist`, in miles unless `-Yu`), so Chicago's
    coordinates legitimately return Bridgeport and Sydney's return Surry
    Hills -- ties broken by table order. The assertion that survives is
    the one the code actually implements: no city the search skipped is a
    whole unit closer than the one it chose. Both drafts were caught by
    sabotaging the search, not by reading it.

    **Leg 12 found a live NULL dereference.** The plan expected
    invariants here and got a bug. `szThereforeDef[]` is declared
    `[cAspect+1]` -- 25 entries -- and its initializer listed 19, so
    `szTherefore[19..24]` were **NULL** rather than `""`. The guard
    `FInterpretAsp()` tests `szInteract[]`, which *is* fully initialized,
    and `intrpret.cpp` then reads `szTherefore[asp][0]` at six sites. The
    switch that reaches it is documented and shipping:

        astrolog -A 24 -YIA 19 "is %sopposed to" -I

    Segmentation fault, core dumped, measured at exit 139. `-YIA` sets
    the interaction text for an aspect; setting one above 18 makes
    `FInterpretAsp()` true for a row whose "therefore" half does not
    exist. **Both builds**, since none of this is guarded -- and the same
    class as `ruler2[]` being one short, which is what `defaults_audit`
    was written for and does not cover here because these are text
    tables rather than numeric ones.

    Fixed by completing the initializer. Causation proven both ways:
    shortening it again brings the segfault straight back, and the suite
    names it as "6 were NULL" rather than waiting for a crash.

    The leg asserts what these tables can actually promise, which is
    **not** that every row has text -- they are sparse on purpose,
    Astrolog interpreting ten aspects and four angles. It asserts that no
    row is NULL at any index the switches reach, and that the populated
    shape stays put: eleven aspects with interaction text, twelve signs
    with all three of theirs.

    **Nets**: suite 3802/0, up 241; oracle group 565/0; chart matrix 0 of
    7,072 lines and switch matrix 0 of 75,635 against a baseline built
    from the previous commit, which is what says the data.cpp change is
    inert everywhere except the crash; four builds and both Qt versions
    clean; eight audits.

172. **T4's other half, and the number it was chasing turned out to be the
    wrong question.** The open item read: `rgswitchdef[]` has 41 rows
    declaring `carg>0`, 31 of them absent from the round-trip fixture,
    and separating settings from imperatives "needs a judgement per row".
    Doing that by hand would have been thirty-one judgements and a
    permanently stale list.

    **The writer is the oracle for "is it a setting."** Nothing that
    `FOutputSettings()` does not emit can round-trip, by definition, and
    everything it does emit should. So the check asks the question
    directly, against the real artifact rather than a regex over source:
    take a settings file the program actually saved, and require every
    value switch in it to be named by some `EXPECT` in the fixture. That
    is leg 3b of `tools/settings-round-trip.sh`, and it took the
    per-row judgement out of the loop entirely.

    Measured that way, the gap was **38 value switches saved with nothing
    asserting they came back** -- among them `-z`, `-z0`, `-zl` and `-zf`
    (time zone, daylight, location, temperature: the chart's own data),
    the four `-YR*` restriction families, `-Yj0`/`-Yj7`, `-Yi1`/`-Yi2`/
    `-Yi3` and `-M0`. Item 140's `-b` family lived in exactly this set,
    unnoticed for five days.

    All 38 now have sentinels, and every one round-tripped on the first
    run, so this bought coverage rather than bug fixes -- 35 assertions
    to 73. Two exemptions, each measured and each recorded beside its own
    line rather than in a list somewhere else:

    - **`:Xb`** (bitmap file type): `NSwXb()` returns `tcError` when
      `us.fNoWrite` is set (switch.cpp:1474), and that is precisely the
      state a settings save runs in. The writer emits a value no
      settings file can set.
    - **`:YXf`** (fonts): the writer emits the aggregate
      `":YXf #%06x"` of `gs.nFontAll`, while the switch sets one
      component at a time through a sub-letter (`YXft`, `YXfs`, ...).
      One line saves what no single line can set.

    **One thing worth knowing came out of it.** `-zl`'s saved form
    follows the *zodiac display format*: with `:sd` set, the default
    location serializes as decimal degrees instead of `100W00`. A display
    switch changing how a stored setting is written is not obvious from
    either switch's description, and it broke this fixture's own
    expectation the moment `:sd` was added three lines above it.

    **Falsified twice**, since a coverage check that cannot fail is
    decoration: corrupting the `-Yw` save-twin makes leg 3 name the exact
    sentinel (`leg 3 MISS: ^-Yw 3\\.5`), and deleting one line's `EXPECT`
    makes leg 3b name the switch (`leg 3b UNCOVERED: :d`). Both restored.

    **Nets**: all three round-trip legs plus 3b; suite 3802/0; eight
    audits; four builds.

173. **The data files become LF, the reason they were not did not
    reproduce, and the crash found on the way there was mine.** Three
    findings, and they have to be read in that order because each one
    was mistaken for the next.

    **The claim.** `.gitattributes` held `*.as` and `*.csv` at CRLF with
    a specific measurement: converting them moved `tools/switch-matrix.sh`
    by six lines, and `-0q` "reported a different chart-info parse
    failure", so "something in the data parsers reads a CR as content".
    That was the one exemption in the LF conversion that was not about a
    binary or a Windows tool.

    **It does not reproduce.** With `astrolog.as`, `atlas.as`,
    `timezone.as`, `astexo.csv`, `nrvate.as` and `mazegame.as` all
    converted, measured on six surfaces:

        switch matrix, 529 invocations       0 of 75,635 lines differ
        chart matrix, 71 invocations         0 of 7,072
        eight atlas + timezone lookups       0
        "-0q" itself, the named invocation   identical
        settings round trip, all four legs   pass
        Windows text charts under Wine       identical to a CRLF capture

    The likely explanation for the original six lines is unhappy: the
    sweep that produced that measurement is the same one that corrupted
    28 binaries, **including every `.se1` the charts are computed from**.
    A chart-info parse failure reading differently is exactly what a
    damaged ephemeris looks like. That is inference, not measurement --
    the evidence is gone -- but the conversion itself is measured, and it
    is clean. They are LF now, and `line_endings_audit.py` covers them:
    101 tracked text files to 108.

    **Then the suite started crashing, 3 runs in 10.** Segfault and
    abort, and the last line before it was always `Writing wireframe to
    file`. There is an open finding about exactly that writer (item 166):
    `WriteWire()`'s loop checks that *one* word remains and then reads
    six. So an hour went into it.

    That work was not wasted -- the writer had a second and worse defect
    next to the one already recorded. A color record is two words for a
    palette index and **three** when the index is invalid and an RGB
    triple follows, and the reader advanced by two unconditionally
    whenever `gs.fColor` was clear. A three-word record then left the
    cursor one word short and everything after it was parsed at the wrong
    offset. Both are fixed: the length is read whether or not the color
    is being written, and each branch checks its own record fits before
    reading it. The graphics matrix is 0 across 224 renders, so this
    changes no output that exists today; it closes a hazard.

    **But it was not the crash.** The crash was `TestNumericOracleQt` at
    qttest.cpp:4596 -- **leg 11, from item 171, three hours old.**
    `DisplayAtlasNearby()` prints a city list through `is.S` on its way to
    returning an index; the "just return the index" early exit lives in
    its `fDialog` branch, which this call did not take. So it wrote to a
    `FILE *` nothing had opened. That is work log item 165's hazard,
    reintroduced **one leg after leg 12's own comment quoted it**, in the
    same session.

    Two lessons, both already written down here and both re-learned:

    - **Do not reason from output ordering.** `PrintProgress()` goes to
      unbuffered stderr; this went to a buffered stream. The wireframe
      message was simply the last thing to escape. CLAUDE.md says this
      in as many words, about the last time it happened.
    - **A backtrace costs one build.** `gdb -batch -ex run -ex bt` on an
      optimized `-g` binary named the line on the third loop, after an
      hour of reading code.

    Fixed by giving the leg a stream of its own, the way legs 9 and 12
    already do. **3 crashes in 10 runs became 0 in 10, then 0 in 6 more
    with the data files converted on top.**

    **Nets**: suite 3803/0 and stable across 16 consecutive runs; switch
    and chart matrices 0 against a CRLF baseline; graphics matrix 0
    against a pre-fix baseline; round trip all four legs; eight audits;
    Windows build and its captures identical.

174. **T5's tail, read one call at a time instead of counted.** The open
    item said 90 formatting calls remained unbounded -- "pointer
    arithmetic, struct members and split-line calls, worth doing
    opportunistically, none of them a buffer whose size the code cannot
    know". Reading them rather than counting them, they were three
    different things and only one mattered.

    **Two were live overflow risks.** `FJPLCachePut()` (io.cpp) guards
    the URL it caches -- `if (CchSz(szUrl) >= cchSzLine*2) return;` --
    and then writes the *name* into a fixed member beside it with no
    guard at all. And `SzObjSelName()`'s seen-list (calc.cpp) copies an
    object name into `rgObjSelSeen[].szName` the same way. Both
    destinations are struct members, where `sizeof` works and `S()`
    applies with no change to the idiom; the reason they were passed over
    was the *shape* of the destination, not any real obstacle to bounding
    them.

    **Six more took `S()` or `SO()` for free**: the executable-directory
    concatenation in `FileOpen()`, which builds a path out of `argv[0]`
    and a user-supplied file name; the Swiss error text on both its
    branches; the `-Ye` point-name suffix, which writes at an offset and
    wanted `SO()`; and three display strings.

    **The rest stay bare, by verdict rather than by omission.**
    `xscreen.cpp`'s two write into a buffer `PAllocate`d to exactly the
    length about to be written, so a bound would restate the allocation
    one line above it. `charts2.cpp`'s are precision-limited formats
    (`%7.7s`, `%3d`, `%2d%%`) into `cchSzMax`, where the format itself is
    the bound. And `sweph.cpp` and its siblings are third-party.

    That is the finding worth keeping: **"90 unbounded calls" was
    measuring the spelling, not the risk.** Two of the ninety could
    overflow, six were free to fix, and the others were already safe for
    reasons the count could not see.

    **Nets**: suite 3803/0; switch matrix 0 of 75,635 and chart matrix 0
    of 7,072 against a baseline built from the previous commit; four
    builds with no new warnings.

175. **T7's worklist, and the six lines that were never about line
    endings.** The last open entry in REFACTORING.md's plan was a
    worklist rather than a design: `tools/warning_audit.py` names the
    remaining hand-rolled save/restore pairs for free, because GCC cannot
    correlate the condition that saves with the condition that restores
    and says so as `-Wmaybe-uninitialized`. That is exactly the property
    `Borrow` removes.

    **Seven converted, one refused, one restructured.** The seven are in
    `DrawSidebar`, `DrawSymbolRing`, `DrawObjects` (twice), `DrawWheel`,
    `DisplayAtlasLookup` and `DisplayAtlasNearby`, plus `PrintChart`'s
    Windows path. Two patterns did the work:

    - **Borrow unconditionally, assign conditionally.** Restoring a value
      that never changed is a no-op, and the save and the restore can no
      longer disagree about whether they happened -- which is the only
      thing those pairs could get wrong, and precisely what GCC could not
      prove. `DisplayAtlasNearby` had an early `return` between its save
      and its restore, so the hand-rolled version leaked `us.fAnsiChar`
      and `us.fGraphics` on that path.
    - **Brace the borrow where the old restore was.** `DrawSidebar` and
      `DrawSymbolRing` restore mid-function and then rescale, so the
      borrow ends at a brace and the rescale follows it.

    **The refusal is the more interesting one.** `CastRelation`'s `rSav`
    looks identical and is not: it captures chart 1's MC *after* casting,
    to reinstate once every chart is done. A borrow taken at function
    entry restores the value from *before* the loop, which is a different
    number. It was converted, measured, and put back, with the reason at
    the line. Initializing it to silence GCC would be inventing a value,
    which item 150 already ruled out for this class.

    And one loop was restructured rather than borrowed: the constellation
    search set `kSav` inside its "found a nearer one" branch and repaired
    it afterwards with `if (h >= rDegMax) kSav = sAri;`. Seeding `kSav`
    with that same answer before the loop is identical in result and
    makes the guarantee local enough for the compiler to see.

    Ledger: **316 warnings in 100 sites to 305 in 96.**

    **Then the harness.** Comparing the three matrices against a baseline
    turned up six differing lines in the switch matrix -- and *six lines*
    is the exact number `.gitattributes` had cited for years as proof
    that the data parsers read a CR as content. It is the same six, and
    they have nothing to do with line endings.

    `run -0q` is nondeterministic. `-0q` makes `Terminate()` return
    instead of exiting, so the program carries on past a fatal error and
    formats whatever the abandoned operation left behind. Measured over
    30 runs of one binary: 28 said "Value 0 out of range from 1 to 12",
    one said "Assuming first century C.E. is really meant instead of
    1905" -- and the year differs every time it appears, 1901, 1902, 1905,
    1908 -- and one said `Unknown function: 'Q\357\277\275'`, a garbage
    byte in a string. **That is an uninitialized read**, not merely
    recovery-mode noise.

    So item 173's inference is wrong and is corrected here: the six lines
    were not the corrupted ephemeris. They were this, and any run of the
    switch matrix could produce them at about one chance in fifteen. The
    invocation is out of the harness with that measurement recorded at
    the line, and the matrix now diffs to zero across three consecutive
    runs of the same binary. **A differential harness has to be
    deterministic or it invents evidence**, and this one invented the
    reason for a two-year exemption.

    *Left open, deliberately* — **and taken the same day, work log item
    176.** It was `InputString()` falling through its EOF branch because
    `Terminate()` returns under `-0q`, and the line that does it was also
    reading 255 bytes into three callers' 80-byte buffers, which is a
    stack smash reachable from a documented prompt with no `-0q` at all.

    **Nets**: suite 3803/0; chart matrix 0 of 7,072, graphics matrix 0 of
    449, switch matrix 0 of 75,629 against a baseline built from the
    previous commit, and 0 across three runs of the same binary; five
    builds including Windows and Qt6; warning ledger regenerated.

176. **The `-0q` uninitialized read, and the stack smash sitting next to
    it.** Item 175 left the read open with its measurement. Hunting it
    took one grep once the shape was right, and turned up a worse bug on
    the way that needs no `-0q` at all.

    **The read.** `InputString()` (io.cpp) ends its EOF branch like this:

        if (fgets(sz, cchSzMax, stdin) == NULL)   // Pressing Control+d
          Terminate(tcForce);                     // terminates the program
        cch = CchSz(sz);                          // on some systems.

    `Terminate()`'s first statement is `if (us.fNoQuit) return;`, and
    `-0q` is what sets `us.fNoQuit`. So under that switch the EOF path
    falls straight through to `CchSz(sz)` on a buffer `fgets` never
    wrote. Every symptom follows: the caller's stack usually holds
    something that parses as 0 ("Value 0 out of range from 1 to 12"),
    sometimes a two-digit number that reads as a year ("Assuming first
    century C.E. is really meant instead of 1905"), and once a byte that
    reached the AstroExpression parser as a function name. No sanitizer
    was needed and none would have been convenient: valgrind is not on
    this machine, MSan wants clang and an instrumented libc, and an ASan
    build times out because `-0q` at EOF **also spins** -- `NInputRange`
    loops until the value is in range, and it never will be.

    **The stack smash, which is the more serious half.** That same line
    read a hardcoded `cchSzMax` -- 255 -- into whatever buffer the caller
    passed. Three callers pass `char[cchSzDef]`, which is **80**:
    `NInputRange`, `RInputRange`, and the scroll pause in general.cpp. So:

        astrolog -i tty        then 251 characters at "Enter month"
        *** stack smashing detected ***, exit 134, core dumped

    `-i tty` is the documented way to enter chart information
    interactively. No `-0q`, no unusual state, both builds, and it has
    been there as long as the prompt has. T5's campaign bounded 1,141
    formatting calls and never looked at the one raw `fgets`.

    **Fixed together**, because they are the same line: `InputString()`
    takes its destination's size like everything else in this tree since
    T5, and every caller passes `S(...)`; `NPromptSwitches()` threads one
    through as well. The EOF branch sets `sz[0] = chNull` and returns, so
    nothing downstream parses an unwritten buffer whether `Terminate()`
    came back or not.

    **Falsified both ways.** Restoring the hardcoded size brings the
    abort straight back (exit 134, then gone again on reverse-patch).
    Restoring the fall-through restores the uninitialized read by
    construction -- though not visibly in 25 runs, which the roughly
    one-in-fifteen manifestation rate explains and does not prove; the
    evidence that it is fixed is that 20 runs after are identical where
    30 before produced three different outputs.

    **And the harness gets its coverage back.** `run -0q` was removed
    from `tools/switch-matrix.sh` yesterday for being nondeterministic.
    It is deterministic now, so it is back, and the matrix diffs to zero
    across three consecutive runs of the same binary.

    *Still not fixed, and stated rather than left silent:* `-0q` at EOF
    spins in `NInputRange` forever. That is what the switch asks for --
    do not quit -- and changing it means deciding that EOF is not an
    error a user can correct, which is a maintainer's call rather than a
    bug fix.

    **Nets**: suite 3803/0; chart and graphics matrices 0 against a
    baseline built from the previous commit; switch matrix 0 across three
    runs with the restored invocation; round trip all four legs; five
    builds including Windows and Qt6.

177. **The oracle reaches the searches, and each one is checked against
    the condition it was searching for.** Item 171 closed the position
    surfaces and left a note: the transit and progression *searches* have
    no outside reference either, but they have leg 9's invariant -- a hit,
    re-cast, must satisfy what the search was looking for. Three legs,
    one per search function, and nothing in the suite had exercised two
    of them at all.

    **Leg 13, the in-day search.** Sun and Moon only, conjunction only,
    with sign changes, direction changes and the void-of-course pass
    turned off: every hit is then a new moon. Six months of 2020, and at
    each hit the two are conjunct to better than 0.01 degrees. **The
    second check is not an internal invariant at all** -- consecutive
    hits must be 29.53 days apart, which is the synodic month, a fact
    about the solar system rather than about this program.

    The restrictions are the finding. Without them the search reports six
    other event kinds, and the first draft asserted that every hit was an
    exact aspect: measured, half of them were 128.9165, 101.0042,
    73.0522 degrees -- sign ingresses and void-of-course entries, all
    correct, none an aspect.

    **Leg 14, the transit search**, in its ordinary mode rather than leg
    9's return mode. Transiting Moon to natal Sun, conjunction only, one
    month. **The sense of the two object tables is the opposite of the
    obvious one**: `ChartTransitSearch()` swaps `ignore` and `ignore2`
    around its `CastChart(-1)`, so `ignore2[]` selects the *transiting*
    objects. Written the other way round first, the search found nothing
    at all, which is how the leg earned its comment.

    **Leg 15, the horizon search**, which has a geometric invariant
    rather than an aspect one: the event's own name says what must be
    true. "rises" and "sets" put the object on the horizon, "zeniths" and
    "nadirs" on the meridian. Measured at 41.85N on 2020-03-20 the
    altitudes come back -0.001 and 0.000 and the azimuths 270.005 and
    90.004, so the tolerances are twenty times the observed error rather
    than a guess -- and the azimuth convention (meridian at 270 and 90,
    not the compass 180 and 0) is taken from that measurement rather than
    assumed.

    **This leg also caught the suite's own shared state**, which is the
    part worth keeping. Run alone it passed; run inside the full suite it
    reported **123** events instead of 4, because an earlier group leaves
    `us.fInDayMonth` set and the search then swept the whole month. It
    borrows the flag off now. QT_TESTING.md says to state a test's
    preconditions rather than inherit them, and this is what inheriting
    them looks like.

    **Falsified one at a time**, each by shifting its own search's
    reported time at the point where the hit is appended to the chart
    list: the in-day leg reports six conjunctions off by more than 0.01
    degrees, the transit leg one, the horizon leg four events in the
    wrong place -- and legs 9 and 14 fail together on the transit
    append, which is the right answer since they share it. All restored.

    An earlier falsification attempt is worth recording because it
    proved nothing: moving the conjunction's angle in `rAspAngleDef`
    changed no result, because `nrvate.as` sets the live aspect angles
    and the default table never reaches the search.

    **Nets**: suite 3812/0, up 9, and 0 failures across 5 consecutive
    runs; oracle group 575/0; chart, switch and graphics matrices 0
    against a baseline built from the previous commit.

178. **Qt5 and Qt6 are both supported, from one build, by the
    maintainer's decision — and the readiness that implies was measured
    rather than promised.** Two directions given on 2026-09-02.

    **Wingdings is closed without work.** Plan item 6 had been open since
    2026-08-26 describing a licensing fact rather than a task. Closed at
    the maintainer's direction; the seven fonts that can ship still do,
    and Qt substitutes for the rest exactly as Windows does.

    **Q8 is answered, and not the way the CI plan recommended.** That
    document had settled on "known to work, kept alive by CI" the same
    morning, with Qt5 as *the* supported configuration. The maintainer's
    answer is both, for a reason the section had already argued and then
    under-weighted: Qt5 is past upstream's open-source support, so the
    port has to be ready for the day a distribution drops it — while the
    maintainer's own machine runs Qt5 today and the users on it are the
    point.

    **This asks for almost nothing, because the build already does it.**
    `Makefile.qt`'s `QT_MAJOR` asks pkg-config which Qt is present and
    builds against the better one, so a single `make qt` is correct on a
    Qt5 box and a Qt6 box alike. Measured: both give `PASS: 3812 passed,
    0 failed` from the same sources on the same day. The `qt6` and
    `qt6-test` targets are not a second configuration — they exist only
    because *this* machine's Qt6 is hand-installed off pkg-config's path.

    **What "ready" actually measures.** Compiling against Qt6 with
    `-DQT_DISABLE_DEPRECATED_UP_TO=0x060800` — which does not warn but
    **removes the declarations** — succeeds with zero deprecation
    diagnostics. So the port uses nothing deprecated on the way to Qt 6.8;
    the readiness is a present-tense fact, not a plan. Falsified by
    restoring one `QMouseEvent::globalPos()` call, which then fails to
    compile ("class QMouseEvent has no member named globalPos") rather
    than warning.

    **And it is deliberately not a gate.** Wiring that flag into
    `tools/warning_audit.py`'s Qt6 leg was drafted and then withdrawn on
    the maintainer's correction: a permanent no-deprecated-API rule makes
    a Qt6-shaped future the thing the tree optimizes for, and Qt5 support
    here is a requirement rather than a legacy. The measurement is
    recorded at Q8 so the next person can repeat it in one command
    instead of inheriting a constraint.

    *For the CI author:* what this position wants is a second **lane** —
    the existing checks run against each Qt — rather than one job that
    keeps a spare build warm. Shape is theirs to choose.

    **Nets**: Qt5 suite 3812/0 and Qt6 suite 3812/0, same sources; the
    deprecation compile above, falsified.

## Features this fork adds to both builds

Everything else in this document is about reaching parity with Windows.
This section is the exception: work that goes *into* the Windows build as
well, because the user whose fork this is decided a good idea should not
be Linux only. It is deliberately shaped so it could be offered to
CruiserOne — no `#ifdef QT` anywhere in the shared or Windows files, and
the Swiss dependent parts guarded with `#ifdef SWISS` the way `DlgCustom`
already guards its own.

### The aspect count can be raised again

Display Settings' "Number of Aspects to Include" could be lowered but
never raised, in **both** builds, for different reasons. Windows'
`DlgDisplay` assigns `us.nAsp = na` before the loop that un-restricts
the newly included aspects, so that loop runs zero times. This port had
lost the loop entirely to a transcription pass. Either way
`AdjustAspectCount()` then recomputed the count straight back down from
the restrictions, so the control silently did nothing and the only way
back up was ticking boxes in Aspect Settings one aspect at a time.

Both fixed 2026-08-30 by putting the assignment after both loops
(`wdialog.cpp`, `qtdialog.cpp`), which is all it takes — the ordering is
the whole bug. Upstream's half carries no `QT` guard, like the rest of
this section.

`TestAspectCountQt()` (`aspect-count`) drives the real dialog up from 11
to 20 and back down to 3. It is falsified two ways: deleting the
un-restrict loop fails all five assertions, and reproducing Windows'
statement order fails all five too — the second is what proves it guards
the ordering rather than the mere presence of a loop. The Windows half
has no equivalent test; `wdialog.cpp` is Windows-only code the Qt suite
cannot reach, and `tools/windrive.sh` cannot read a control's value
(there is no AT-SPI under Wine), so it rests on the shared behavioural
claim and inspection. Said plainly here rather than left to look
covered.

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

**Every behavioural divergence here names the test that holds it, and
that rule is not decoration.** A divergence is the one kind of claim no
audit can check: `rc_audit`, `rc_mnemonic_audit`, `rc_field_audit` and
`rc_lookup_audit` all compare this port *against* `astrolog.rc`, so
anywhere it intends to differ is outside what they can see by
construction. Prose was the only record, and prose does not fail. When
that was tested for the first time on 2026-08-30 (work log item 132),
one of five rows had silently reverted to the Windows bug months
earlier and another described a fix that would have been a bug. Adding
a divergence without a test is how the next one rots. Falsify each one
*against the Windows behaviour specifically* — reproducing Windows'
version must fail the test, or it only proves "something happens here".

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
  *Held by `TestDialogArrowKeysQt()` (`arrow-keys`).*

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
  - `TestAnimationStateQt()` covers it (`animation`); reverting the
    change fails ten of its assertions.

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
  *Held by `TestLineDrawingQt()` (`line-drawing`), which renders the
  same aspect grid with `gs.nFontTxt` off and on and demands the bytes
  match. It compares two renders rather than hunting for particular
  characters because `-Ya` encodes the same rules as one byte or three,
  and because the grid's size depends on whichever objects the previous
  group left unrestricted — the first two drafts failed in the full run
  while passing alone, once for each of those reasons. Widening the
  `#ifdef WIN` to include QT makes 3,930 bytes differ.*

| Where | Difference | Why | Held by |
|---|---|---|---|
| Chart info dialogs | Daylight shows and offers "Autodetect" | Windows resolves `dstAuto` via `DstReal()` before display, discarding the user's "work it out for me" choice on the next OK. Showing it survives a round trip. | `divergences` — resolving it away on display fails the round trip |
| Command line dialog | Doesn't save/restore `us.fLoop`/`is.fMult` around the call | `CommandLineX()` does. Only matters for a typed line that itself starts a multi-chart sequence. | **nothing yet** — the only observable is a typed line that starts a multi-chart run, which the in-process suite has no way to drive. Recorded as untested rather than left to look covered. |
| Restriction dialogs | (Since 8.9) checkbox = restricted, matching Windows | Previously "Show X" = visible, i.e. inverted. Flipped *toward* Windows, but it's a visible change to anyone used to the old Qt wording. | not a live divergence — it *matches* Windows now, so it is a historical note about older Qt builds, and `dialog-buttons` already pins the sense through `ignore[]` |

**Two rows left this table on 2026-08-30** (work log item 132), and the
reasons are worth keeping:

- **Display Settings' aspect count** was a divergence — this port
  un-restricted the newly included aspects and Windows didn't — until
  the loop that did it was deleted by a transcription pass and the port
  quietly matched the bug for months. Both builds are fixed now, so it
  is no longer a divergence at all; it moved to "Features this fork
  adds to both builds".
- **Graphics Settings' Atlas City Coloring** claimed the port writes
  `gs.fLabelCity` where Windows writes `gs.fLabelAsp`, calling the
  latter an upstream typo. Writing the test showed the note was wrong,
  not the code: `-H` documents `-XL[1-5]` as setting "how to color
  cities (**when -XA is on**)", and `xcharts0.cpp:2088` and
  `xcharts1.cpp:174` both read `gs.fLabelAsp` for exactly that. `-XL`
  is whether cities are plotted at all, so the "fix" this row proposed
  would have wired the colouring combo to the wrong control. The port
  writes `fLabelAsp`, matching Windows, and `divergences` pins it —
  taking this row's old advice fails two of its assertions.

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

- Build with `make qt -j4`; binary is `./astrolog-qt`.
- Compile-check after every change before testing live — this codebase
  has caught real preprocessor/linkage mistakes this way every session.
- **Run the test suite before every commit.** `make qt-test` then
  `./run-qt-tests.sh` — headless, no display needed, exits nonzero on
  failure. Assertions covering dialog titles, the 42 context menus,
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
- **The source is LF**, since 2026-09-01 (work log item 159). It used to
  be split — upstream's sources CRLF, this fork's own LF — and the rule to
  preserve that per file was broken four times before being caught, and
  twice more after (items 145, 158). Nothing in the source needed CRLF:
  converting left all 64 object files byte-identical across both
  toolchains. Binaries, Windows tooling, `font/` and the `.as`/`.csv`
  data files are exempt, each for a reason `.gitattributes` states.
  `tools/line_endings_audit.py` checks the rest. **Never run a
  CR-stripping sweep over the tree** — one did, with a three-extension
  exclusion list, and corrupted 28 binaries.
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
