# qttest.cpp review

A piece-by-piece read of `qttest.cpp` (13,282 lines on 2026-09-13),
looking for actual bugs, smells, and areas for improvement. Findings are
appended as the read proceeds; the phased action plan at the end is
written once every line has been covered. Nothing is fixed during the
review -- the same rule as `POST_BUILD_REVIEW.md`.

Severity: **P1** a bug that can produce a wrong verdict (a false pass or
a false fail) or crash the suite; **P2** a weakness that makes a check
prove less than it claims; **P3** a smell or clean-up with no behavioural
consequence.

Line numbers are as of the commit `028b4ba` working tree. **The read is complete** (2026-09-13); the action plan is at the end. Commit `dcd939b` landed after the read and is reviewed in its own section before the plan; the plan reflects it.

## Coverage so far

| Range | Groups covered |
|---|---|
| 1-2200 | header, harness, Dialogs, About, Context menus, Hotkeys, Chart rendering, Firing every menu item, Menu parity, Menu extras, Bad input, DriveModalQt, Over-long command line, File recursion, Graphics Settings fields, Chart export, Dialog fit, App icon |
| 7480-13282 | ObjSel parse, Chart info time, Chart list filter, Ephemeris list, Relationship modes, Shared core fixes, Rulership, Esoteric tables, Nested include, Settings fields/arrays/strings sweeps, Graphics mode source, Registry, Forced positions, captures, Probe, Matrix Julian, Numeric oracle (15 legs), Atlas sink/zone, Eclipses, Lockdown, Copy text BOM, Restrict recall, Printing, Open dir, Save suffix, Chart mode table, Cast cooking, File parsers, Line drawing, Long strings, Ray digit fill, Aspect count, Aspect dash, Star links, Star sort, Divergences, the table and runner |
| 2200-7480 | Console font, Colour scheme, Restriction buttons, Shared symbols, Clear Screen, Rising gradient, Text export, Apply Info, Now buttons, Orbit buffer, Font pack, Menu side effects, Graphics size, Combo picks, Field parse, Orb grid, Screen colours, Notice, Chart scroll, Pager, Key help, Text extent, Screen options, Export round trip, Null names, Jet trail, Window size, Credit colours, Transit modes, Chart store, Atlas apply, Animation state, Menu resync, OK settles, Mnemonics, Arrow keys, Midpoint glyph, ObjSel glyph/lookup, Settings round trip, Interface settings, Custom dialog parse, ObjDefSet, Timer sanity, ObjSel dialog, Swiss enumerate, Fill bounds, Restrict object names, Transit restrict, ObjSel table |

## Findings

### Working-tree state

- **W1 (P1, working tree, not committed) -- RESOLVED by `dcd939b`.** The
  line is gone from the tree; it was the other session's instrument for
  the bug that commit fixes (see "Commit dcd939b" below). `Group()` at line 160 had an
  uncommitted debug line, `printf("[st %s: ignAsc=%d]\n", ...)`, printing
  `ignore[oAsc]` before every group header. It is the only pending change
  to the file. It changes the suite's stdout for every group, so anything
  that greps the log (the run script, `tools/ci-run-suite.sh`) sees an
  extra line per group. Another session may own it (see the shared-tree
  rule); it must not be staged by accident and must not ship.

### Harness (lines 1-360)

- **H1 (P3).** `#include <QtCore/QSet>` appears twice (lines 45 and 63).
- **H2 (P3).** The comment block at lines 121-128 says the same thing
  twice in two paragraphs ("The bundled ephem/ and the body list are one
  set ..." / "The bundled ephem/ and the Object Selections list are one
  set by construction ..."). One should go.
- **H3 (P3).** `rgdlgQt[]` has **26** rows; CLAUDE.md, QT_TESTING.md and
  the suite's own printed summary elsewhere say "25 dialogs". The About
  dialog was added to the table and the prose was not updated. The
  suite prints the real count, so this is documentation drift only.
- **H4 (P2).** `StrOpenDialogQt()`'s safety net (`tNet`) closes only
  `activeModalWidget()`, while the primary timer also falls back to
  `activePopupWidget()`. A dialog that opened as a popup and whose first
  close did not take would hang the run, since the net cannot see it.
  Cheap to make symmetric.
- **H5 (P3).** `StrOpenDialogQt()` keeps `strTitle` as a function-static
  so capture-less lambdas can reach it. `DriveModalQt()` (line 1414),
  written later, uses `std::function` with captures and is the cleaner
  shape; the dialog test could be written on top of it and the static
  removed. Not reentrant as it stands, but nothing reenters it.
- **H6 (P3).** `Check()` formats into `cchSzMax` with `vsnprintf`, which
  is right, but a failing message carrying a 300-character settings
  marker (the sweeps use 300-character markers on purpose) is truncated
  in the FAIL line. Consider `cchSzLine` for the harness buffer; it costs
  nothing.

### Dialogs, About (lines 358-460)

- **D1 (P3).** Two near-identical comment blocks introduce `szVersionGit`
  (lines 385-395 and 402-405), and the forward declaration of
  `DriveModalQt()` sits between them with its own explanatory comment.
  The forward declaration exists only because the About group was placed
  before the helper; moving `DriveModalQt()` up beside `StrOpenDialogQt()`
  removes the declaration and one of the comments.
- **D2 (P3).** `TestAboutVersionQt()` picks the first `QLabel` whose text
  contains "version". Fine today; brittle if a second label ever mentions
  the word. Matching on the `szVersionCore` string instead would be
  unambiguous.

### Chart rendering (lines 552-675)

- **R1 (P2).** The first loop judges "blank" by comparing every sampled
  pixel against `pixel(0,0)`. The menu-firing group directly below it
  learned, at measured cost ("It was the check that was blind, not the
  chart", 23 items at once), that the corner holds the border and in
  monochrome the border is the drawing colour, and it compares against
  `gi.kiOff` instead. The rendering group still uses the corner. It
  happens to pass because it runs early with colour on, but it is the
  same blind spot and would misreport a monochrome run. Use the same
  `KvFromKi(gi.kiOff)` comparison, ideally through one shared helper
  (the `CpixDifferQt()` at line 2763 may already be it -- to confirm).
- **R2 (P3).** `cmode` is computed as `sizeof(rgnMode) / sizeof(int)`,
  and `rgszMode[]` is a parallel array with no compile-time check that the
  two have the same length. A `static_assert` or a single struct table
  (as `rgt[]` in the Graphics Settings group does) removes the hazard.

### Firing every menu item (lines 677-915)

- **M1 (P3).** The header comment (line 683) still says "Skipped: anything
  whose label ends in '...' (those open a dialog and would block; the
  dialog test covers them)". The code stopped skipping those: it fires
  them and a repeating timer closes the modal. Stale comment describing
  behaviour the group no longer has; a reader trusts it and reasons
  wrongly about what is covered.
- **M2 (P3).** The macOS skip matches `str.contains("rint")`, which is
  broad (any future label containing "rint" is skipped silently). The
  count assertion at the end pins it to exactly two, which is the real
  guard, so this is a legibility point: match the two labels.
- **M3 (P3).** Three `char [cCust][cchSzMax]` arrays on the stack,
  `cCust` being `9 + 9 + cMoons2`, is on the order of 30-50 KB of frame
  for a save/restore of strings that are almost always the defaults. Not
  a bug (the main thread's stack is 8 MB) but the largest frame in the
  file; a `QVector<QByteArray>` or saving only the non-default slots
  would do.
- **M4 (P2).** The restore of custom slots (lines 866-885) restores
  `rgTypSwiss/rgObjSwiss/rgPntSwiss/rgFlgSwiss` and glyph/display strings,
  but the sweep may also have changed `rgObjName[]`/`szObjName` for those
  slots via `-Yeb`'s renaming, and any other per-object state a macro's
  `.as` reaches (colours, orbs, `rgobjset[]`). The comment says "three
  later groups assert untouched-slot preconditions" -- if any of those
  reads a field this restore misses, the suite depends on files outside
  the repository, which is exactly the failure this restore was written
  to stop. To check against the three groups when they are reached.

### Menu parity / extras (lines 921-1340)

- **P1 (P3).** `TestMenuParityQt()` performs the `PaFindLooseTestQt()`
  lookup before testing `fSkip`, so the twelve skipped rows are looked up
  for nothing. Trivial, but the `cwrong` counter is also incremented and
  never reported; either print it or drop it.
- **P2 (P2).** `TestMenuExtraQt()` matches a Qt-only leaf against the
  parity table by label with `&` removed. Two distinct Qt items with the
  same de-ampersanded text as one Windows item would both pass (the
  "one command wired in twice under two names" case the header says the
  group exists for is covered only when the second name is *new*). A
  duplicate-label check over the Qt leaves themselves would close it.

### Bad input (lines 1345-1400)

- **B1 (P3).** Five assertions are `Check(fTrue, ...)`. Reaching them is
  the test, as the comments say, but each counts as a pass and inflates
  the total, and the pattern is unique to this group. A `Reached()`
  helper that prints in verbose mode and does not count, or simply a
  comment, would keep the totals honest. (These are the only five
  `Check(fTrue` in the file.)

### Orphaned and misplaced comment blocks (P3, structural)

A recurring smell: functions were moved or added and their header
comments were left where they were, so several blocks now describe a
function far away or one that does not follow them. Each one misleads
the reader who trusts the comment above a function to be about it:

- Lines 1399-1412: "The Object Selections list is a table of {type,
  index, name} triples ..." sits directly above `DriveModalQt()`; it
  describes `TestObjSelTableQt()` (line 7391).
- Lines 1489-1497: "Does the port follow the desktop into dark mode?"
  and "The application icon ..." sit above `DialogShotCaptureQt()`; they
  belong to `TestColorSchemeQt()` (2336) and `TestAppIconQt()` (2135).
- Lines 1542-1554: "Does every control actually FIT inside the dialog"
  sits above `TestLongCommandLineQt()`; it belongs to `TestDialogFitQt()`
  (2046).
- Lines 1660-1672: "Every 'Save Chart As' format ..." sits above
  `TestFileRecursionQt()`; it belongs to `TestChartExportQt()` (1953).

More will be listed as they are found; the fix is one pass moving each
block to its function, and it needs no test.

### Over-long input, recursion, Graphics fields (lines 1563-1950)

- **L1 (P3).** `TestFileRecursionQt()` writes its chain files with
  `QFile::open()` and ignores a failure silently; a full or read-only
  temp directory would make the "refused" half pass for the wrong reason
  (nothing to include). Assert each `open()`.
- **G1 (P3).** In `TestGraphicsFieldsQt()`, the OK-button click loop is
  copied eight times inline (`for (QPushButton *ppb : ...) if (text() ==
  "OK") { click(); return; }`). `ClickOkInModalQt()` (line 5489) and
  `ClickInModalQt()` (2534) exist further down; the duplication predates
  them. Worth one helper `FClickButtonQt(pw, "OK")`.
- **G2 (P2).** The first two `DriveModalQt()` calls in the group set the
  delay field and click OK but do not `return` after the click, so if
  the dialog had two "OK" buttons both would be clicked; harmless here,
  but the later lambdas in the same function do `return` -- two idioms in
  one function for the same action.

### Chart export, dialog fit, icon (lines 1953-2170)

- **E1 (P2).** `TestChartExportQt()` leaves `is.szFileOut` pointing at a
  stack buffer (`szPath`) for the duration of the loop and restores it
  after; correct, but the `Borrow` RAII helper used ten lines above for
  `gs.chBmpMode` would make the six manual save/restores in this function
  exception- and early-return-safe. The function has no early return
  today; the point is consistency within one function.
- **E2 (P3).** `TestDialogFitQt()`'s first loop duplicates
  `StrClippedChildQt()` (line 220) with a different tolerance (0 vs 1 px)
  and a different visibility test (`isHidden()` vs `!isVisible()`). Two
  implementations of "is a child outside its dialog" in one file will
  disagree one day; keep one, parameterised on tolerance.
- **E3 (P3).** Lines 2112-2118: two consecutive comment paragraphs both
  begin "A check that examined nothing would pass too"; the first is a
  leftover draft of the second.

### Ownership of strings and struct copies (lines 2200-7480)

The file's own header rule (line 730) is "never by assigning a saved US
or GS back, since both carry char * fields other code frees", and
several groups have learned the same lesson for `szObjDisp[]` at
measured cost (a macOS release broke on it). The rule is violated or
skirted in these places:

- **S1 (P2).** `TestMidpointGlyphQt()` (line 5760) does `US usSav = us;
  GS gsSav = gs; ... us = usSav; gs = gsSav;` -- exactly the forbidden
  pattern, in the same file as the rule. Nothing between the copy and
  the restore reallocates a `us`/`gs` string field today, so it is safe
  by luck; the group pins about ten fields by hand anyway and could
  restore those ten instead.
- **S2 (P2).** `FCloneSz(szPath, &is.szFileOut)` in
  `TestExportRoundTripQt()` (4574) and `TestWindowSizeQt()` (4764), with
  the previous pointer saved and put back afterwards. `FCloneSzCore()`
  either frees the old buffer or, when it is large enough, **copies the
  new text into it in place** -- so if `is.szFileOut` is ever non-NULL
  when the suite runs (a `-o` on the command line), the restored pointer
  is either dangling or now holds the scratch path. `FSaveSettingsToQt()`
  (5469) already does this correctly by assigning the pointer;
  `TestSettingsRoundTripQt()` and the others assign directly too. Use
  one idiom. The clone is also never freed.
- **S3 (P2).** `TestOkSettlesQt()` (5537) says in its own header that
  "a copy taken before an OK holds pointers the OK has since freed" and
  restores settings by replaying a file for that reason -- and then ends
  with `ciMain = ciSav; ciCore = ciCoreSav;`, a struct copy taken before
  26 OKs, one of which (Set Chart Info) `FCloneSz()`es `ciCore.nam` and
  `.loc`. Safe today only because the dialog writes back text of the same
  length, which `FCloneSzCore()` copies in place. Restore the eight
  numeric fields the group compares, not the struct.
- **S4 (P2).** `TestNullNamesQt()` (4669) and `TestExportRoundTripQt()`
  both do `is.cci = 0; FAppendCIList(...)` and then put `is.cci` back.
  `FAppendCIList()` writes into `is.rgci[0]`, so if a chart list is
  loaded when the suite runs its first entry is silently replaced by a
  nameless chart or by the probe chart. Save and restore `is.rgci[0]`,
  or append at the real `is.cci` and truncate.
- **S5 (P3).** `TestExportRoundTripQt()` leaks its two `SzClone()`s
  (`ciWant.nam`, `ciWant.loc`). The core leaks the same way on every
  `-qb`, so this is consistency, not a defect, but the suite counts
  unfreed allocations at exit in release builds.
- **S6 (P3).** The `CI` struct save-and-restore idiom (`CI ciSav =
  ciCore; ... ciCore = ciSav;`) appears in about fifteen groups. The core
  itself never frees `nam`/`loc` (every writer uses `SzClone()`), only
  the Qt dialogs do, so it is safe wherever no dialog OK intervenes. A
  one-line helper comment at the first use, or a `struct CIPin` that
  saves the eight numeric fields and nothing else, would make the safe
  cases obviously safe and the unsafe ones (S3) stand out.

### Environment and process state not restored

- **V1 (P2).** `TestColorSchemeQt()` (2336) calls
  `qunsetenv("ASTROLOG_QT_THEME")` at the start of its theme block and
  never restores the value the process started with. A developer running
  the suite under `ASTROLOG_QT_THEME=dark` to check the dark scheme gets
  the first 20 groups dark and the remaining 100 under the saved
  preference, with no indication. Save with `qgetenv()` and put it back.
- **V2 (P3).** The same group ends `SetThemePrefQt(saved);
  ApplyColorSchemeQt(); QApplication::setPalette(palWas);` -- the last
  line overrides what the line before it just applied. One of the two is
  redundant; if the intent is "exactly the palette we started with", the
  `ApplyColorSchemeQt()` call is the one to drop, with a comment.

### Buffer sizes

- **U1 (P2).** `CReplaySettingsQt()` (line 6110) reads the settings file
  with `fgets(szLine, cchSzMax, file)`. A settings line can be far longer
  than 255 characters -- the file's own header says AstroExpressions and
  star lists run to hundreds, and the sweeps use 300-character markers
  so that truncation is caught. A long line is split in two and the tail
  is run as a second command line. None of the five filters that use it
  today select a long switch, so it is latent; make it `cchSzLine` and
  it stays latent when a sixth filter arrives.
- **U2 (P3).** `FEqSzPrefixQt()` ends with `return szLine[i] <= ' '`.
  With `char` signed, any byte above 0x7F also satisfies it, so a UTF-8
  character directly after a switch would count as end-of-switch.
  Irrelevant to the switches replayed, noted for completeness.

### Duplicated logic that has a helper elsewhere in the file

- **C1 (P3).** The inline "find the OK button and click it" loop appears
  23 times in lambdas, in two shapes (`return` after the click,
  and not). `ClickOkInModalQt()` (5489), `ClickInModalQt()` (2534) and
  `TickInModalQt()` (2552) exist; a `FClickButtonQt(QWidget *, CONST char
  *)` used by all of them and by the lambdas would remove a few hundred
  lines and make the two shapes one.
- **C2 (P3).** "Find the button whose text is Cancel and click it" has
  the same duplication, and is done three ways: by text `"Cancel"`, by
  `findChild<QPushButton *>("IDCANCEL")`, and by `text().contains("Cancel")`.
  One of the three is the right one (the object name, which survives
  relabelling); the other two should become it.
- **C3 (P3).** The blank-image test (sample every N pixels, compare to a
  reference colour) is written out by hand in at least eight groups,
  with the sample step 2, 3 or 4 and the reference `pixel(0,0)`,
  `pixel(1,1)`, or `gi.kiOff`. `CpixDifferQt()` is the helper for one
  variant. One helper taking the step and a reference colour would end
  the drift noted in R1.
- **C4 (P3).** Pixel-counting over the whole image is repeated with
  `& 0xffffff` in some groups and `| 0xff000000` in others to drop alpha.
  Same helper.

### Individual findings, lines 2200-7480

- **F1 (P3).** `TestConsoleFontQt()` passes a formatted string as the
  *format* argument: `sprintf2(S(sz), "%s is bundled...", name);
  Check(..., sz)`. Harmless while no font name contains `%`, but it is
  the one place in the file where a data string is a format string; use
  `Check(..., "%s is bundled and loaded", name)`.
- **F2 (P3).** `TestMenuSideEffectsQt()` saves and restores
  `gs.fHouseExtra` (`fHouseSav`) but nothing in the group touches it.
  Dead save.
- **F3 (P3, superseded by K8 below).** `TestRisingGradientQt()`,
  `TestTransitModeQt()` and `TestChartScrollQt()` set `ignore[]` for
  their own object set; `TestChartScrollQt()` and the restriction groups
  call `AdjustRestrictions()` after, the other two do not. Commit
  `dcd939b` showed the real problem is one level up: see K8.
- **F4 (P3).** `TestRisingGradientQt()` uses `Borrow` for four fields
  and hand save/restore for `us.fGraphics`, `gi.nMode` and `ignore[]` in
  the same function; `TestChartExportQt()` likewise mixes the two. Pick
  one per function.
- **F5 (P3).** `TestNoticeQt()` and `TestInterfaceSettingsQt()` call
  `FProcessCommandLine((char *)"literal")`. The function takes `CONST
  char *` (astrolog.cpp:393), so the cast is unnecessary and misleading
  -- it reads as if the callee might write into a string literal.
- **F6 (P3).** `TestGraphicsSizeQt()`: "bit when the group ran alone" is
  a typo for "bit" as in "was bitten"? Reads as "but". Clarify.
- **F7 (P3).** `TestScreenOptionsQt()` defines `cnModeScreenOpt` with a
  `#define` inside the function body and `#undef`s it at the end;
  `TestAnimationStateQt()` does the same with `FRunningQt()`. A `const
  int` / a small `static` function is the C++ shape.
- **F8 (P3).** `TestOkSettlesQt()`: `NSettlesQt()` returns `-1`, `fTrue`
  or `fFalse` and the caller compares `n == fFalse`; a tri-state through
  `flag` constants. An enum, or two out-parameters, reads better.
- **F9 (P3).** `TestDialogMnemonicsQt()` sends `QKeyEvent(KeyPress,
  Qt::Key_A, NoModifier, "s")` -- the key code is always `Key_A` and only
  the `text()` varies, so the assertion holds only while the dialog's
  handler reads `text()`. Send the matching key code so it holds either
  way.
- **F10 (P3).** `TestObjSelGlyphQt()` picks `rgcb[1]` as "row 1's body
  combo", `TestFontPackQt()` picks `rg[0]` as the text slot,
  `DriveObjSelQt()` picks `rgcb[5]` and `rgx[5]` as row 5 -- ordinal
  coupling to `findChildren()` order, which is creation order and not
  guaranteed by layout. The custom-dialog group solved the same problem
  by geometry; the ObjSel dialog could give its rows object names via
  `rc2qt.py`'s `nIdx` so lookups are by index.
- **F11 (P3).** `TestOrbGridQt()` has four lines over 80 columns; the
  file is otherwise 80-column. (A count for the whole file is in the
  tooling section below.)
- **F12 (P3).** `TestFillBoundsQt()` allocates `gi.qim`/`gi.qpaint` with
  `new` and swaps them in; a Qt warning inside `DrawChartX()` that longjmps
  or throws does not exist, so this is fine, but the message handler is
  installed process-wide and restored -- if `Check()` inside the loop
  ever aborts, the handler leaks. Note only.
- **F13 (P3).** `s_nAnimStartQt` is a non-`static` global with an `s_`
  prefix (line 2746). Every other file-scope variable here is `static`.
  Make it static.
- **F14 (P3).** The comment above `TestRisingGradientQt()` has a broken
  line: "packs either one bit per\n// object" (line 2848).

### More orphaned / misplaced comment blocks (continuing the list above)

- 2740-2755: "Animation: one switch, and only the switch moves it" sits
  above `CpixDifferQt()`; belongs to `TestAnimationStateQt()` (5162).
- 2836-2845: "The one text-capture dance, and the global it is easy to
  forget" sits above the Rising gradient comment; belongs to
  `TestTextExportQt()` (2894).
- 3123-3135: "Chart for Now, which is not the same command in a
  relationship chart" sits above `TestNowButtonQt()`; belongs to
  `TestChartNowQt()` (4236).
- 3245-3260: "gs.nFontAll, the packed form..." sits above the Graphics
  size comment; belongs to `TestFontPackQt()` (4005).
- 3262-3275: "The chart size typed into Graphics Settings" sits above the
  Menu side effects comment; belongs to `TestGraphicsSizeQt()` (3306).
- 3400-3470: a run of **seven** blocks in reverse order of their
  functions: Combo picks (3917), Field parse (3857), Orb grid (3771),
  Screen colours (3702), Notice (3638), Chart scroll (3549), then Key
  help which is right. Reads like the functions were reordered and the
  comments were not.
- 4700-4720: "Timed Exposure (gs.fJetTrail)..." sits above the Screen
  options comment; belongs to `TestJetTrailQt()` (4724).
- 4830-4837: "The credits box under Reverse Background" and "The chart
  window's size limits" sit above `TestTextExtentQt()`; they belong to
  `TestCreditColorsQt()` (4838) and `TestWindowSizeQt()` (4764).
- 5350-5355: "Windows dialogs act on a mnemonic letter pressed on its
  own" sits above the "Menu check marks" banner; belongs to
  `TestDialogMnemonicsQt()` (5610).
- 5870-5880: "Pointing a slot at a different body drops the old body's
  glyph" sits above `ResetCustomSlotQt()`; belongs to
  `TestObjSelGlyphQt()` (5911).
- 7150-7162: "One resource dialog, dlgRestrict, serves two commands" sits
  above `TestRestrictObjectNamesQt()`; belongs to
  `TestTransitRestrictQt()` (7317).

This is now 20-odd instances. The tally suggests a mechanical cause (a
sort or a bulk move) rather than 20 separate slips, and the fix is one
careful pass. Because several of the displaced comments carry the
*evidence* for a group -- measured numbers, the bug it caught -- a reader
who finds the comment above the wrong function loses that evidence.

### Compiler warnings (lines 12792-12960) -- the one finding a tool already reports

- **T1 (P1).** `tools/warning_audit.py --file qttest.cpp` reports **six
  `-Wunused-variable` warnings in `TestSortStarQt()`**: `fArabicSav`,
  `nSortSav`, `nPartSav`, `szAbbrev`, `szSignFull` and `i`. CLAUDE.md
  says `tools/warnings.txt` is empty and "a new line is a warning to fix,
  not to record", so the standing warning audit is red as of commit
  `028b4ba` ("Standing suite legs for the trigger paths..."), which added
  this group. `make check` does not run the warning audit, which is how
  it got in. Three of the six are save variables whose restore was never
  written -- see T2.
- **T2 (P2).** `TestSortStarQt()` saves `us.fArabic`, `us.nStarSort` and
  `us.nArabicParts` and restores none of them. The legs run `-Un`, `-S`,
  `-Pn`, `-HO` and `-R0` through `FProcessCommandLine()`, which sets
  `us.nStarSort`, toggles `us.fOrbit`, `us.fArabic` and
  `us.fOrbitData`, and `is.fHaveInfo` is set explicitly; the group also
  assigns `us.nArabicParts = cPart`. Only `us.fInterpret`, `us.fGraphics`,
  `us.nRel`, `ignore[]` and the chart info are put back. Every group after
  it (95 of them) inherits a sorted star table, an orbit-chart flag and
  whatever `-Pn` left. The group is 10th in the table.
- **T3 (P2).** Line 12897-12898: `fileT = fopen(szFile, "r");` twice in a
  row. The first `FILE *` is leaked every run. A copy-paste slip.
- **T4 (P3).** `SORTSTAR_LEG(command)` is a five-statement macro with an
  `#undef` at the end of the function; a small static function taking
  the command string does the same with a type and a stack frame.
- **T5 (P3).** `fFortune` is initialised to `fFalse` at declaration and
  again before use.

### State leaks into later groups (lines 7480-13282)

Groups that leave the process different from how they found it, beyond
what their comments admit. Each one is a "passes alone, fails in the
suite" trap waiting for the next group that reads the field:

- **K1 (P2).** `TestForcedPositionsQt()` (9316) runs `-WM 1
  "AstrologQtSuiteMacro"` to prove the macro name is saved, and never
  restores macro slot 1's name. Every later group that saves settings
  writes it, and the Macro menu shows it for the rest of the run.
  `TestInterfaceSettingsQt()` does the same experiment and restores.
- **K2 (P2).** `TestDivergencesQt()` (12966) sets **every** combo box in
  Graphics Settings whose current text is "None" to "Rainbow", then
  restores only the three city-label fields. The decoration fill combo
  (`gs.nDecaFill`) offers "Rainbow RYB" and the store loop matches by
  prefix, so it takes the value too and is not put back. Address the
  city-colour combo by object name (`dcGr_XL`, as `TestComboPickQt()`
  does).
- **K3 (P2).** `TestStarLinksQt()` (12734) saves `gs.szStarsLin` and
  `gs.szStarsLnk` into `cchSzLine` (1020-byte) stack buffers with
  `sprintf2()` and restores through `FProcessYXU()`. The settings sweep
  two hundred lines above says the constellation star list "alone runs to
  thousands of characters". A run whose settings carry such a list gets
  it silently truncated at 1019 characters for the rest of the suite --
  and the later `settings-fields` sweep then snapshots and faithfully
  restores the truncated one. Latent: `nrvate.as` carries `-YXU "" ""`.
  Save into `QByteArray` as `TestRestrictObjectNamesQt()` does.
- **K4 (P2).** Chart-list clobbering, the S4 family, is wider than the
  two groups first listed: `is.cci = 0` appears **12 times**
  (`TestChartListFilterQt` three times, `TestOpenDirQt` twice, oracle
  legs 9, 13, 14, 15, `TestNullNamesQt`, `TestExportRoundTripQt`). The
  oracle legs save the first eight entries and put them back; the others
  save the count only. A run with a chart list loaded from the settings
  file has its first entries overwritten. One helper -- save `is.rgci[]`
  up to `is.cci` into a `QVector<CI>`, restore both -- closes all twelve.
- **K5 (P2).** `TestChartListFilterQt()` (7851) appends three charts to
  whatever list exists and then asserts the filtered list shows exactly
  3 rows. That is true only if `is.cci` was 0 on entry, an undocumented
  precondition; with a list loaded, the group fails on the first
  assertion for a reason unrelated to the filter.
- **K6 (P3).** `TestSharedCoreFixesQt()`, `TestLineDrawingQt()` and
  `TestLongStringsQt()` do `FCloneSz(szOut, &is.szFileScreen)` and then
  `FCloneSz(NULL, &is.szFileScreen)` -- a run started with `-os <file>`
  loses the setting. Save and restore the pointer, as the `szFileOut`
  groups do (with the S2 caveat).
- **K7 (P3).** `TestAspectDashQt()` (12654) assigns a string literal to
  `us.szExpAsp` and `TestSharedCoreFixesQt()` a static array to
  `gs.szSidebar`; both are restored, and only `-YXt` ever `FCloneSz()`es
  the sidebar, so nothing frees a literal today. The same file's own
  groups use `SzClone()` for this. Consistency; and a `DeallocateP()` of
  a literal is a crash with no useful backtrace.

### The numeric oracle (lines 9635-10800)

- **O1 (P3, structural).** `TestNumericOracleQt()` is ~1,070 lines and
  fifteen legs in one function body, with `ciMainSav2`, `ciMainSav3`,
  `ciMainSav4` and repeated `rgciSav[8]` blocks because each leg needs
  its own locals. Each leg is already self-contained (own borrows, own
  scratch stream, own restore); make each a `static void OracleLegNQt()`
  and the outer function a list. The legs' numbering in the comments
  (1, 2, 3, **5, 4, 4b**, 6...) is also out of order in the source.
- **O2 (P2).** Leg 4b: `for (iDeg = 0; iDeg < 1; iDeg++)` twice, with
  `1` hard-coded where `sizeof(rgDegen)/sizeof(*rgDegen)` belongs. The
  comment right above it explains how the set went from seven entries to
  one; the next change to the set leaves the loop reading only the
  first.
- **O3 (P3).** The `cGood = 1; ... Check(cGood == 1, "the oracle restored
  every borrowed setting")` at the end is a tautology: `cGood` is
  assigned unconditionally and the `Borrow` destructors have already
  run. Either assert a borrowed field's value after the block, or drop
  it (it is a sixth `Check(fTrue)`).
- **O4 (P3).** The `#ifndef SWISS` branch cannot compile: `rgoracle[]`
  uses `SE_SUN` and friends outside any guard. Either guard the table too
  or remove the dead branch.
- **O5 (P3).** The Leg 4b failure message reads "(%d appear changed
  fixed -- if that is deliberate, drop them from rgDegen)" -- garbled
  text.
- **O6 (P3).** The eclipse leg asserts exact counts (`cSol == 60 && cGap
  == 55 && cLun == 40`); good. The comment above says "59 midpoints" where
  the assertion says 55. One of the two is stale.

### Settings sweeps (lines 8500-9210)

- **W1 (P2).** `TestSettingsStringsQt()`'s header says the marker "is
  over cchSzMax again, because -YD and -YU formatted their value through
  sz until this group was written". Only the **macro** markers are long
  (`SzSetFieldMarkQt`); the object-name marker is `"%.3sProbe%d"` and
  the star marker `"StarProbe%d"`, both under 20 characters. The
  truncation regression the comment claims to catch for `-YD`/`-YU` is
  not caught. Either lengthen the markers (object names may have a
  length cap in `NParseSz()`, which the comment hints at) or correct the
  claim.
- **W2 (P3).** `rgsetskip[]` lists `us.nProgress` twice (lines 8556 and
  8592) with different reasons; the second is unreachable.
- **W3 (P3).** `TestSettingsArraysQt()` computes `cbElem` twice with the
  same nested ternary, and formats its "was/is" strings with the comma
  operator inside `if/else` arms (`sprintf2(...), sprintf2(...)`). A
  small helper for each.
- **W4 (P3).** A comment line at 8700 runs to 120 columns ("...rewrites
  nearly every setting there is. The strings are copied, not pointed
  at:") -- a merge left two sentences on one line.

### Misc, lines 7480-13282

- **N1 (P3).** File-static "result" globals used to pass a value out of
  a lambda -- `s_strCombo`, `s_strComboWin`, `s_strTimField`,
  `s_cRowList`, `s_strRow0`, `s_strLookupQt`, `s_cTickQt` -- predate
  `DriveModalQt()` taking a `std::function`; every one can be a local
  captured by reference, as the newer groups do. `s_cRangeMsgQt` and
  `s_cAtlasRow` legitimately must be static (C callbacks).
- **N2 (P3).** `TestObjSelParseQt()` phrases its messages as failures
  ("did not read as asteroid 7066", "the list match is case sensitive
  when it shouldn't be") where every other group states the positive
  claim. The FAIL line then reads "FAIL ... did not read as ...", which
  is right, but a verbose pass listing would read wrong. Cosmetic.
- **N3 (P3).** Parallel arrays `rgnMode[]`/`rgszFile[]` in
  `GraphicsChartCaptureQt()` and `rgszAct[]`/`rgszFile[]` in
  `TextChartCaptureQt()`, like R2.
- **N4 (P3).** `TextChartCaptureQt()` pins `ciCore`/`ciMain`, `gs.xWin`,
  `us.fGraphics` and never restores; acceptable for a capture mode that
  exits, but it and `DialogShotCaptureQt()` run without
  `SetNoPopupQt(fTrue)`, so a warning box inside a dialog hangs a
  screenshot run -- the exact hazard the runner sets the flag for.
- **N5 (P3).** Magic `48` as the chart-mode flag snapshot size in
  `TestChartModeTableQt()`, `TestLineDrawingQt()`, `TestLongStringsQt()`
  (each asserting `cchartmode <= 48`), while `TestPrintQt()` and
  `TestCopyTextBomQt()` use `QVector<flag>(cchartmode)`. Use the vector.
- **N6 (P3).** `TestLineDrawingQt()` keeps a `static char szFont0[65536]`
  for a file comparison; a `QByteArray` read of both files and `==`
  is shorter and has no cap.
- **N7 (P3).** `TestNestedIncludeQt()` and `TestGraphicsModeSourceQt()`
  `fopen()` scratch files and `fprintf()` without a NULL check; a full
  temp directory is a segfault rather than a FAIL.
- **N8 (P3).** `TestEclipseQt()` takes `US usSav = us` only to read
  scalar fields back, and also keeps four separate `*Sav` locals for
  fields in the same struct; one mechanism.
- **N9 (P3).** `NRunQtTestsQt()` returns `fFalse` from three branches and
  `0` from the fourth.
- **N10 (P3).** Variable shadowing: `TestSharedCoreFixesQt()` redeclares
  `nModeSav`, `xWinSav`, `yWinSav`, `fTextSav` in inner blocks that shadow
  the function's own; `TestCopyTextBomQt()` declares `int i` and then
  `for (int i ...)`. `-Wshadow` is not on, so nothing reports it.
- **N11 (P3).** Style: 20 lines over 80 columns in an otherwise
  80-column file, most of them the `sprintf2(S(szPath), "%s/...",
  QDir::tempPath()...)` idiom -- which is itself repeated ~30 times and
  wants a `SzScratchPathQt(char *, int, CONST char *szStem, CONST char
  *szExt)` helper.
- **N12 (P3).** `getenv("ASTROLOG_QT_TEST_VERBOSE")` is called at four
  sites, once per rendered item inside loops; one file-static `flag`
  read at startup.
- **N13 (P3).** `TestAtlasSinkQt()`'s "console city lookup" leg sets
  `pfnAtlasRow = NULL` and calls `DisplayAtlasLookup(..., fFalse, ...)`,
  which prints the city rows through `PrintSz()` into `is.S`.
  `TestAtlasZoneQt()` installs `SinkAtlasRowQt()` for exactly this
  reason. Worth checking the suite log for a stray city listing.

### More orphaned comments (7480-13282)

- 7530-7545: three blocks above `StrEphemListQt()` -- "Forced object
  positions..." (`TestForcedPositionsQt`, 9316), "Two bugs in shared
  upstream code..." (`TestSharedCoreFixesQt`, 8116), "A GUI casts the
  same relationship chart repeatedly" (`TestRelationshipModeQt`, 8014).
- 7640-7660: three above `NExpEvalQt()` -- "Windows' DlgList narrows..."
  (`TestChartListFilterQt`), "Windows fires the redraw notification
  hook" (`TestExpressionHooksQt`), "The accelerator column..."
  (`TestAccelTextQt`).
- 7643: "Windows leaves an ephemeris out of this list" above
  `FilterChartListQt()`; belongs to `TestEphemerisListQt()`.
- 12273: "Work log item 115: ... pinned across the whole text chart
  surface" above `TestLineDrawingQt()`; belongs to
  `TestLongStringsQt()` (12412), whose own comment then follows the
  wrong one.

Final tally: about **30 displaced comment blocks**. In every case the
displaced comment is *above* a function that appears *earlier* in the
file than the one it describes, which is what a pass that moved
functions but not their leading comments would produce.

## Commit dcd939b, reviewed against this document

"The star-sort legs leave the state they found, and say what is true"
(2026-09-13 12:31), touching `TestSortStarQt()` only. Three changes:

1. **`szFile` was never written.** The capture path buffer was declared
   and passed to `CaptureTextToFileQt()` unfilled, so every leg wrote a
   chart dump to a garbage-named file in the working directory and read
   the same garbage name back -- green, self-consistent, and the source
   of the ~35 untracked files with binary names that `git status` showed
   when this review began. The commit builds the name once, in the temp
   directory. **A real P1 the review did not list**: the read of that
   region happened after the other session had already fixed it in the
   working tree, so the reviewed text was the corrected one. The junk
   files in the initial `git status` were the symptom and should have
   been connected to the group. They are gone from the tree now.
2. **`AdjustRestrictions()` -> `RedoRestrictions()` after restoring
   `ignore[]`.** `AdjustRestrictions()` recomputes only `is.nObj`;
   `RedoRestrictions()` (general.cpp:986) re-derives the six category
   flags `us.fCusp/fUranian/fDwarf/fMoons/fCOB/fStar` from `ignore[]`
   and `ignore2[]` and then calls `AdjustRestrictions()`. The `-R`
   family ends in `RedoRestrictions()` (switch.cpp:2053), so `-R0`
   inside the group had set `us.fCusp = 0`, and restoring the bytes of
   `ignore[]` without re-deriving left it there; the next group's cast
   then re-restricted the cusps, and **every group from `text-pager`
   on ran with the Ascendant restricted.** Verified by the instrumented
   `Group()` line (finding W1). Correct fix, and it generalises -- K8.
3. Two comments corrected to say what an instrumented sweep measured
   (`us.nArabicParts` is 177 at every group entry, not 0; `-qb` sets
   `is.fHaveInfo` itself).

**What the commit did not do**, all still open and re-verified against
the tree after it:

- **T1** stands: `tools/warning_audit.py --file qttest.cpp` still reports
  the same six `-Wunused-variable` in `TestSortStarQt()` (`fArabicSav`,
  `nSortSav`, `nPartSav`, `szAbbrev`, `szSignFull`, `i`). The commit
  message says the legs "leave the state they found"; three of those
  six are the save variables for state they still do not restore.
- **T2** stands: `us.fArabic`, `us.nStarSort`, `us.nArabicParts`,
  `us.fOrbit`, `us.fOrbitData` are still changed by the legs' switches
  and not put back. (`ignore[]` and the category flags now are.)
- **T3** stands: the doubled `fopen()` is at lines 12901-12902 now.

### K8 (P2, new, from the commit's evidence) -- restoring `ignore[]` needs `RedoRestrictions()`, not `AdjustRestrictions()`

Every group that saves `ignore[]`/`ignore2[]` bytes, changes
restrictions, and restores the bytes ends with `AdjustRestrictions()`
(16 call sites in 12 groups) or with nothing. That is correct only if
nothing in between re-derived the category flags. Two things do:

- any `-R`-family switch (`RedoRestrictions()` at the end of the handler),
  which reaches the groups that replay settings lines carrying `-YR`:
  `TestSettingsRoundTripQt()` (replays `-YR` with the moon slot
  poisoned), and the two settings sweeps (which restore `us` scalars
  from a snapshot, so they are covered by accident);
- the Qt restriction dialogs' OK, which calls `SyncRestrictMenuQt()` to
  re-derive `us.fUranian`/`us.fDwarf` and their menu checks
  (qtdialog.cpp:5218; `TestMenuResyncQt()` asserts exactly this), which
  reaches `TestDialogButtonWiringQt()` ("Restrict All" then restore),
  `TestTransitRestrictQt()` and `TestObjSelDialogQt()` (the Show box).

So at least four groups can leave a category flag stale after restoring
the array under it, which is the shape that just cost a sweep of the
whole suite. The fix is mechanical -- `RedoRestrictions()` at every one
of the 16 sites, since it is a superset -- and cheap to prove: the
instrumented `Group()` the commit used, printing `ignore[oAsc]` and the
six flags at each group entry, is the falsification. Better still, make
that instrument permanent: see plan item 3a.

### Summary counts, revised

| Severity | Before dcd939b | After |
|---|---|---|
| P1 | 2 (W1 tree, T1) + 1 unlisted (the garbage `szFile`) | 1 (T1) |
| P2 | ~25 | ~25 (K8 added, F3 folded into it) |
| P3 | ~50 | ~50 |

## Summary

| Severity | Count | Themes |
|---|---|---|
| P1 | 2 | W1 uncommitted debug print in `Group()`; T1 six compiler warnings in the newest group (the warning audit is red) |
| P2 | ~25 | state leaks between groups (T2, K1-K5, V1, M4), string-ownership fragility (S1-S4, S2 idiom in three groups), latent buffer limits (U1, K3), checks that claim more than they test (R1, W1, O2, P2, G2), a leaked FILE (T3) |
| P3 | ~50 | ~30 displaced comment blocks, duplicated idioms (23 OK-click loops, 12 `is.cci = 0`, ~30 scratch-path sprintfs, 8 blank-image loops), tautological checks, dead saves, parallel arrays, magic numbers, shadowing, long lines |

What is **not** wrong: the assertions themselves. Every group I read
asserts both directions where a one-sided check would pass vacuously,
and most carry the measurement that justified the bound. The file's
weakness is hygiene between groups and duplication, not the tests.

## Phased action plan

Each phase is independently committable and `make check` clean. Phases
1 and 2 are small and should go first; phases 3-5 are refactors that
must each be proven behaviour-preserving by the suite's own count
staying identical (the count is printed; record it before and after).

### Phase 1 -- today, before anything else ships (T1, T2, T3)

1. ~~Resolve the uncommitted debug line in `Group()`~~ -- done by
   `dcd939b`, which was what it was instrumenting. Instead: bring it
   back **on purpose** as plan item 3a.
2. `TestSortStarQt()`: delete the six unused variables **or** write the
   restores they were declared for (T2 -- prefer the restores:
   `us.fArabic`, `us.nStarSort`, `us.nArabicParts`, `us.fOrbit`,
   `us.fOrbitData`, `is.fHaveInfo`); remove the duplicated `fopen()`.
   Verify with `tools/warning_audit.py --file qttest.cpp` reporting
   nothing.
3. Add the warning audit for `qttest.cpp` alone to `make check` (the
   full audit is 70 s; one file is seconds) so a red warning audit
   cannot land through the pre-commit command again. `dcd939b` went in
   with the six warnings still present, which is the second commit in a
   row to do so.
3a. **A permanent group-entry canary**, behind `ASTROLOG_QT_TEST_VERBOSE`
   or a new `ASTROLOG_QT_TEST_CANARY`: at each `Group()`, print (or
   diff against the table-start snapshot) the handful of globals the
   leftovers list above `TestAllMenuActionsQt()` names -- `ignore[oAsc]`,
   the six category flags, `us.nRel`, `us.fGraphics`, `is.cci`, `is.S`,
   macro slot 1's name, `gs.szStarsLin`'s length. Two sessions have now
   built this by hand with a throwaway `printf` (the commit's sweep, and
   work log item 57's "dump the globals and diff them"); K1-K5 and K8
   are all things it would have shown on the first run.

### Phase 2 -- state leaks and false claims (K1-K5, K8, V1, O2, W1, R1, M4)

Do K8 first: it is one search-and-replace and it is the class the commit
just paid for.

3b. K8: `RedoRestrictions()` at all 16 `AdjustRestrictions()` sites that
   follow a restore of `ignore[]`, and after the two restores that call
   nothing (`TestRisingGradientQt()`, `TestTransitModeQt()`). Falsify
   with the canary from 3a: before the change, drive
   `TestDialogButtonWiringQt()` and read the flags at the next group's
   entry.

Each is a five-line fix with a falsification: break the thing the
group depends on, run the group alone and in the full suite, confirm
both fail; fix; confirm both pass.

4. K1: restore macro slot 1's name in `TestForcedPositionsQt()` (copy
   the save/restore from `TestInterfaceSettingsQt()`).
5. K2: address the city-colour combo by name in `TestDivergencesQt()`.
6. K3: save the star lists into `QByteArray` in `TestStarLinksQt()`.
7. K4/K5: write `struct ChartListPin { QVector<CI> rgci; int cci; }`
   with save/restore, use it at all 12 `is.cci = 0` sites; make
   `TestChartListFilterQt()` start from an empty list explicitly.
8. V1: save and restore `ASTROLOG_QT_THEME` in `TestColorSchemeQt()`.
9. O2: `sizeof(rgDegen)/sizeof(*rgDegen)` at both loops; O5/O6 fix the
   two stale comments while there.
10. W1: decide whether the `-YD`/`-YU` markers can be long (check
    `NParseSz()`'s object-name length limit); lengthen them or correct
    the comment.
11. R1: make `TestChartRenderQt()` compare against `gi.kiOff` like the
    menu-firing group; falsify with a monochrome run.
12. M4: check the "three later groups" that assert untouched-slot
    preconditions against what the menu-actions restore covers
    (`rgObjName`/`szObjName`, `rgobjset[]`, colours); add whatever a
    macro's `.as` can reach and the restore misses.

### Phase 3 -- string ownership (S1-S4, K6, K7)

13. Write `struct CIPin` (eight numeric fields) and use it in
    `TestOkSettlesQt()` (S3) and wherever a dialog OK intervenes
    between a `CI` copy and its restore.
14. Replace `US usSav = us; ... us = usSav` in `TestMidpointGlyphQt()`
    with the ten field pins it already knows about (S1).
15. Replace the two `FCloneSz(szPath, &is.szFileOut)` with the pointer
    assignment `FSaveSettingsToQt()` uses (S2); save/restore
    `is.szFileScreen`'s pointer in the three `Action()` groups (K6).
16. `SzClone()` instead of literal/static assignment for `us.szExpAsp`
    and `gs.szSidebar` (K7), freeing on restore.

### Phase 4 -- the comment pass (~30 blocks)

17. One careful commit, no code change: move every displaced comment
    block to sit directly above the function it describes, using the
    list in this document. Check by reading each moved block's first
    sentence against the function name under it. `git diff --stat`
    should show only `qttest.cpp` and equal insertions/deletions.
    Remove the two duplicated paragraphs (H2, D1, E3) in the same pass.

### Phase 5 -- deduplication and structure (C1-C4, N1, N3, N5, N11, N12, O1, B1, O3)

Behaviour-preserving; the suite's assertion count must not move except
where a tautological check is removed (B1, O3 -- record the new count).

18. `FClickButtonQt(QWidget *, CONST char *szText)` and
    `FClickIdQt(QWidget *, CONST char *szObjectName)`; replace the 23
    OK loops and the 16 Cancel variants; standardise on the object name
    (`IDOK`/`IDCANCEL`) where it exists.
19. `CpixNotBackgroundQt(CONST QImage &, QRgb kvBack, int step)` for the
    8 blank-image loops; `SzScratchPathQt()` for the ~30 temp-path
    sprintfs (fixes most of the 20 long lines).
20. Struct tables instead of parallel arrays (R2, N3); `QVector<flag>`
    instead of `flag rgfSav[48]` (N5); one verbose flag (N12); locals
    instead of the seven result statics (N1); `static` on
    `s_nAnimStartQt` (F13).
21. Split `TestNumericOracleQt()` into one static function per leg,
    called from a short driver (O1); drop the dead `#ifndef SWISS`
    branch or guard the table (O4); replace `cGood` with a real
    assertion (O3).
22. `Reached()` or plain comments for the five `Check(fTrue)` (B1).

### Phase 6 -- optional hardening (U1, U2, H4, H6, L1, N7, N13)

23. `cchSzLine` line buffers in `CReplaySettingsQt()` and
    `TestForcedPositionsQt()`; `cchSzLine` for `Check()`'s own buffer.
24. `activePopupWidget()` in `StrOpenDialogQt()`'s net (H4); assert
    `fopen`/`QFile::open` results in the scratch-file writers (L1, N7).
25. Run the suite once with output captured and grep for a city listing
    from `TestAtlasSinkQt()` (N13); install the sink if it is there.

### What to leave alone

The `Borrow` vs hand save/restore mix (F4, E1) is worth unifying only
inside a function being touched for another reason. The 1e-9 pinned
Matrix cusps in `TestCastCookingQt()` have held on three compilers and
should stay. The `-0X`/`-0q` lockdown assertions, the settings sweeps'
design, and every "both directions" pair are the right shape and are
the reason to trust this file.
