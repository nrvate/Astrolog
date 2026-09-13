# Working the QTTEST_REVIEW.md plan

The record of implementing the phased plan appended to
`QTTEST_REVIEW.md`: what each item turned out to be once the code was in
front of us, how each fix was falsified, the gotchas hit on the way, and
anything that could not be fully resolved. One commit per item, on branch
`qttest`, for a squash merge to `qt` after review.

## Setup

- **Branch `qttest` lives in a linked worktree, `/nvm/work/qttest`,** not
  in `/nvmraid/shares/Astrolog`. A second Claude session had that checkout
  as its working directory when the work started; switching the shared
  tree's branch would have put that session's next commit on `qttest`
  (or this one's on `qt`). The worktree shares the object store, so the
  branch is visible from the main checkout as usual, and the main tree
  stays on `qt` untouched.
- **Gotcha: `tools/check.sh` writes the suite log to fixed paths in
  `/tmp`** (`/tmp/check-suite.log`, `/tmp/check-suite.out`). Two trees
  running `make check` at once overwrite each other's logs, so a `make
  check` in the worktree while the main tree runs one reports whichever
  finished last. Not fixed here (out of scope); run them one at a time.
- **Baseline, before any change (`dcd939b`):** `./run-qt-tests.sh`
  reports `PASS: 5198 passed, 0 failed`. Every item below states its count
  against this; an item that moves it says why.

## Items

### Tooling built for this pass (not committed on its own)

**A per-group leak probe.** Before committing anything, a temporary pair
of functions went into `NRunQtTestTableQt()`: snapshot `us` and `gs`
bytewise before each group, and after it walk `rgsetfield[]`
(`settingsfields.h`, every scalar member of both) and print
`[leak <group>: <field>]` for each one that differs. It was the fastest
way to replace "which fields does this group leave changed?" reading with
a measurement, and it was right where reading was wrong (item 2). It
became plan item 3a's canary; see there. What it cannot see, and every
use below respects: `is`, `gi` and `ci*` (no generated field table),
string members (pointer identity, not content), and the arrays --
`ignore[]`, `rgobjset[]`, colours.

Run against `dcd939b` it named **92 changed fields in 15 groups**:
66 in `menu-actions` (expected -- it fires every menu item and says so),
and 26 elsewhere. Kept at `/nvm/work/qttest-leaks-baseline.txt` for the
duration of the work; the items below cite it.

### Plan item 2 -- T1, T2, T3: `TestSortStarQt()`

**What the review said and what was measured.** T2 listed the fields
"changed by the legs' switches" by reading the switch names:
`us.fArabic`, `us.nStarSort`, `us.nArabicParts`, `us.fOrbit`,
`us.fOrbitData`. The leak probe, run over the unmodified group, names a
different five:

| Field | Set by | In T2? |
|---|---|---|
| `us.fOrbit` | `-S` toggles it | yes |
| `us.fArabic` | `-Pn` toggles it; the group then pins it true | yes |
| `us.fObject` | `-HO` toggles it (`switch.cpp`, the `-H` family) | **no** |
| `us.nArabicSort` | `-Pn` -- the *parts* sort, not the star sort | **no** |
| `us.nStarSort` | `-Un` | yes |

Two of T2's were wrong: **`-HO` is the object table, `us.fObject`; the
orbit data is `-HS`**, so `us.fOrbitData` was never touched. And
`us.nArabicParts`, which the group assigns to `cPart`, does not show
because it is already 177 (`cPart`) at every group entry -- dcd939b's own
comment says so; restoring it anyway costs nothing and is right in a run
whose settings file sets another value. `us.fStar`, which `-Un` toggles
three times, does not show because `RedoRestrictions()` re-derives it from
the restored `ignore[]`. `is.fHaveInfo` (set by `-qb`) is outside the
probe's reach and is restored on reading the code alone.

**The change.** Restores for all five measured fields plus
`us.nArabicParts` and `is.fHaveInfo`, with a comment naming which switch
sets each; the three dead declarations (`szAbbrev`, `szSignFull`, `i`)
removed; the doubled `fopen()` removed. T4 (the macro) and T5 (the
double initialisation of `fFortune`) are P3s with no plan item and were
left alone.

**Falsified.** `tools/warning_audit.py --file` over `dcd939b`'s
`qttest.cpp` (copied into the tree root so its includes resolve) reports
exactly the six T1 warnings; over the fixed file it reports nothing.

**Gotcha: `warning_audit.py --file` exits 0 when it prints warnings.**
It returns nonzero only when the file does not compile; the report is the
output. So "exit 0" is not "clean", and any gate built on it -- plan item
3 -- has to fail on non-empty output. Measured on the pre-fix file: six
warning lines, `exit 0`.

**A new finding the probe turned up, not fixed here: `text-pager`
repaints over leaks.** In the baseline, `text-pager` (the group straight
after `star-sort-outputs`) *also* reported `us.fOrbit`, `us.fArabic` and
`us.fObject` changed -- because `TestPagerQt()` calls
`SetChartModeQt(gWheel)`, which clears the chart-type flags, and its
restore `SetChartModeQt(nModeSav)` re-selects a mode without putting the
flags back. So it silently undid star-sort's leak for everything after
it, and it will equally clear any chart flag a settings file turned on.
Two consequences: a leak upstream of `text-pager` is invisible to every
group downstream of it, which is a reason the T2 leak never failed
anything; and the probe cannot attribute blame for flags `SetChartModeQt()`
owns without a before/after at both groups.

**Suite.** `PASS: 5198 passed, 0 failed`, unchanged, as expected -- the
restores change what later groups inherit, not what any group asserts.
The leak probe's diff against the baseline lost exactly eight lines and
gained none: the five `star-sort-outputs` fields, and the three
`text-pager` fields that were only ever star-sort's leak being cleared
downstream.

### Plan item 3 -- the one-file warning audit in `make check`

**The change.** A `warnings: qttest.cpp` step in `tools/check.sh`, straight
after the test binary builds, running `tools/warning_audit.py --file
qttest.cpp`. Measured at 8.6 s on this machine (with the suite running
beside it), against 70 s for the full five-build audit that stays out of
the fast loop.

**Gotcha, and the reason the step is a function rather than a plain
`step`:** as recorded under item 2, `--file` exits 0 when it prints
warnings. A plain `step "..." python3 tools/warning_audit.py --file
qttest.cpp` would have been green over exactly the six warnings it was
added to catch. The step's function fails on a nonzero exit (the file
does not compile) *or* on any output at all.

**Falsified.** The function alone, over `dcd939b`'s `qttest.cpp` copied
into the tree root: `rc=1`. Over the fixed file: `rc=0`, no output. Run
in isolation rather than through `make check`, because `make check`
rebuilds the test binary and a suite was running against it in the same
worktree.

**Not resolved: only `qttest.cpp`.** The plan asked for this file and the
reason is specific to it -- it is the file that changes most, and two
consecutive commits put warnings in it. Every other file still reaches
the warning ledger only through the full audit, which nothing runs
unless someone remembers. Adding the rest one file at a time would cost
about nine seconds each; widening it is a maintainer's call about how
long `make check` may take, so it is left as stated here.

### Plan item 3a -- the group canary, `ASTROLOG_QT_TEST_CANARY`

**What the plan asked for** was a fixed list printed at each `Group()`:
`ignore[oAsc]`, the six category flags, `us.nRel`, `us.fGraphics`,
`is.cci`, `is.S`, macro slot 1's name, the star list's length.

**What was built is wider, and a diff rather than a dump.** The leak probe
from item 2 had already shown that a fixed list is the wrong shape: the
fields T2 guessed were not the fields that leaked, and a list somebody
chose is exactly the vocabulary problem the settings sweeps were written
to escape. So the canary snapshots before each group, in the runner
(`NRunQtTestTableQt()`, not `Group()` -- the runner is where a table
entry's boundaries are, so an "after" exists for the last group too and
the name printed is the one `ASTROLOG_QT_TESTS` selects by), and after the group prints one line per thing that differs:

- every non-string member of `US` and `GS` -- 269 of the table's 342;
  the other 73 are `char *` -- by name, old and new value, through
  `rgsetfield[]`, which covers the six category flags, `us.nRel` and
  `us.fGraphics` from the plan's list;
- every slot of `ignore[]` and `ignore2[]`, with the object's name;
- all 96 macro names and 8 macro submenu names, by content;
- `gs.szStarsLin` and `gs.szStarsLnk`, by content (lengths printed);
- `is.cci` and `is.fHaveInfo`.

`is.S` is left to the check the runner already had, which *fails* rather
than reports and repairs the pointer.

**It reports; it never fails.** `menu-actions` changes dozens of settings
by design, and several groups' lasting changes are their purpose. The
useful outputs are the diff between two runs' canary lines, and the
answer to "what did the group before mine leave behind?" A closing line
counts changes and groups.

**Gotchas while writing it.**

- `ignore[i]` goes through `GRDOBJB::operator[]`, which asserts `i` is
  below `cObj` in a `-DQTTEST` build. The canary walks `rgn[]` directly to
  `objMax` so it never trips the assertion on the slots past `cObj`.
- `flag` is `int`, so `'f'` and `'i'` fields share a comparison; `real`
  fields compare by bytes, not `==`, so a NaN left in a field (the
  settings sweeps poison with them) is not reported as changed on every
  group.
- The sizes come from the field's type letter, not from the gap to the
  next offset, because `US` has padding.

**What it cannot see**, stated in its comment: `is`, `gi` and the `ci*`
structs beyond the two fields named (there is no generated table for
them), string members of `US`/`GS` other than the star lists, `rgobjset[]`
and the colour arrays. Each of those is a place a leak can still hide.

**Suite.** With the canary on: `PASS: 5198 passed, 0 failed` and
`[canary: 208 changes left behind by 16 groups]`. Off: `PASS: 5198 passed, 0 failed` and not one line containing
"canary" -- the default output is unchanged.

**Checked against the probe.** Every scalar line names the same field in
the same group as the item-2 probe did (less the eight lines item 2
fixed). So the scalar leg is the probe, and the probe was right.

**Falsified, by leaks that were already there.** Each leg the probe did not
have reported something on its first run, and each report is a known or
checkable real leak rather than a planted one:

- **The string leg:** `forced-positions: macro name 1 "Default Planets"
  -> "AstrologQtSuiteMacro"`. That is review finding **K1**, open and
  unfixed at this point -- the canary found it with nobody telling it to
  look at macros. (K1 is plan item 4; the canary line is its
  falsification.)
- **The restriction leg:** `oracle: ignore2[2 Moon] 1 -> 0`. **A new
  finding, not in the review:** the numeric oracle leaves the Moon
  unrestricted in the transit set. Recorded under "New findings" below
  rather than fixed inside this item.
- **The star-list leg:** `menu-actions` and `menu-side-effects` each
  change both star lists' content.

**What the first run says about `menu-actions`.** 186 of the 208 lines.
Besides the 66 scalars the probe saw, it leaves **130 restriction slots
changed** -- nearly every moon, star and cusp unrestricted, the
planets and Vulcan restricted in both sets, Earth unrestricted -- and
both star lists rewritten. Its header already says it leaves settings
dirty, but the leftovers list above it does not mention restrictions, and
every group after it runs with that set. That is review finding M4's
territory (plan item 12) and is taken up there.

**Gotcha: canary lines can be split by the program's own output.** One
line in the first run read `[canary transit-restrict: us.fUraCreating
graphics chart in memory.`, with the rest of the canary line somewhere
later in the log. `PrintProgress()` (general.cpp) writes to **stderr**,
which is unbuffered; the canary writes to **stdout**, which is
block-buffered when `run-qt-tests.sh` sends both to one file with `2>&1`.
So a progress message lands in the file at the moment it is printed, in
the middle of whatever stdout block is still pending. A grep for
`\[canary ` still finds the line; a parser that expects the closing `]`
on the same line does not. Compare canary output with that in mind, or
run with stdout line-buffered through a pty (`tools/ci-run-suite.sh`
gives it one).

The first canary run is kept at `/nvm/work/qttest-canary-baseline.log`
for the rest of this work.

## New findings (turned up while working the plan)

- **N-A (P2) `oracle` leaves `ignore2[oMoo]` cleared.** Measured by the
  canary (item 3a). Not yet traced to a leg.
- **N-B (P3) `text-pager` clears chart-type flags it did not set.**
  `SetChartModeQt(gWheel)` then `SetChartModeQt(nModeSav)` does not put
  the flags back; it silently undid star-sort's T2 leak for every later
  group (item 2).

### Plan item 3b -- K8: `RedoRestrictions()` where a group restores `ignore[]`

**What the sites turned out to be.** The review counted "16 call sites in
12 groups". Read one by one, the 16 `AdjustRestrictions()` lines in
`qttest.cpp` are two different things:

- **12 restores** -- the call right after a group puts `ignore[]` (and
  sometimes `ignore2[]`, the `ignoreMem` copies, or all of `us`) back:
  `dialog-buttons`, `chart-scroll` (its second call), `ok-settles`,
  `midpoint-glyph` (second), `objsel-glyph`, `settings-roundtrip`,
  `objsel-dialog`, `transit-restrict`, `shared-core` (second),
  `settings-fields`, `settings-arrays`, `line-drawing` (second). All 12
  now call `RedoRestrictions()`.
- **4 set-ups** -- the call right after a group restricts objects for a
  leg (`chart-scroll`, `midpoint-glyph`, `shared-core` and `line-drawing`,
  each the first of its two). These stay `AdjustRestrictions()`: they run
  before anything could have re-derived the flags, and changing a leg's
  set-up changes what the leg measures, which no plan item asked for.

Plus the two restores the review found calling nothing at all,
`rising-gradient` and `transit-mode`: each gets `RedoRestrictions()`
after its restore loop. (They were also not recomputing `is.nObj`.)

**Why it is safe everywhere.** `RedoRestrictions()` (general.cpp) only
*reads* `ignore[]`/`ignore2[]`, writes the six category flags from them,
and ends by calling `AdjustRestrictions()` -- read in full before the
change, so "superset" is checked, not assumed.

**Falsified before the change, per group, with the canary.** Each of the
15 affected groups run alone (`ASTROLOG_QT_TESTS=<group>`, the canary on,
the binary from item 3a), so the only state a group can have touched is
its own:

- `dialog-buttons`: `us.fUranian 0 -> 1`, `us.fDwarf 0 -> 1` -- the
  dialog-OK route K8 named (`SyncRestrictMenuQt()` re-derives the flags
  from "Restrict All", the group puts the array back, the flags stay).
- `settings-roundtrip`: `us.fMoons 0 -> 1` -- the `-YR` replay route K8
  named. Neither is a suite-order artefact: both leak from a clean start.
- `transit-restrict`, `objsel-dialog`, `objsel-glyph`, `menu-resync`:
  *no* flag change alone. Their full-suite flag lines (in the item-3a
  baseline) come from the state they inherit, which K8's fix at the
  groups before them is expected to remove; the full-suite run says.

**After the change, the same 15 solo runs.** Exactly three canary lines
gone -- `dialog-buttons`' `us.fUranian` and `us.fDwarf`, and
`settings-roundtrip`'s `us.fMoons` -- and every other line, and every
group's assertion count, identical. So the fix removes what K8 described
and nothing else, and no group's legs depended on the stale flags.
`tools/warning_audit.py --file qttest.cpp`: empty.

**The full suite, canary on:** `PASS: 5198 passed, 0 failed`. Against
the item-3a canary baseline, **every category-flag line is gone** --
`transit-restrict`, `objsel-dialog` and `objsel-glyph` (`us.fUranian`),
`dialog-buttons` (`us.fUranian`, `us.fDwarf`) and `menu-resync`
(`us.fDwarf`) -- including the four groups that did not leak alone, which
were therefore inheriting the stale flags from `dialog-buttons` and
`settings-roundtrip` upstream. `menu-actions`' own flag lines remain;
that group changes restrictions by design (item 12).

**Gotcha: two canary runs never diff clean, because of the clock.**
The only other lines that moved were `gs.rRot` and `gs.rTilt` in
`chart-render`, `divergences`, `menu-actions` and `shared-symbols` --
different values in each run (120.729 in one, 122.164 in the next). The
globe's rotation follows the current time, so any group that draws a
globe leaves a time-dependent value. Filter those two fields out before
diffing canary logs.

**Gotcha: the canary prints reals with `%g`, and a change can read as
`120.729 -> 120.729`.** It compares bytes, so the change is real; six
significant digits hide it. Not changed in this item -- a wider format
would move every real line in the baseline, and the fix belongs with the
canary, not with K8. Read an equal-looking pair as "differs below the
sixth digit".

**Other things the solo runs show, not K8, recorded rather than fixed:**

- **N-C (P3) `gs.yWin 600 -> 575`** after every group that opens a
  restriction or Object Selections dialog (`dialog-buttons`,
  `transit-restrict`, `objsel-dialog`, `objsel-glyph`). Plausibly the
  window being laid out again around a dialog under the offscreen
  platform; not traced.
- **N-D (P3) `us.fListing 0 -> 1`** after `rising-gradient`,
  `transit-mode`, `chart-scroll` and `shared-core` -- every one of them
  restores with `SetChartModeQt(nModeSav)`, the same shape as N-B.
- **N-E (P3) `dialog-buttons` leaves `us.nAsp 5 -> 0`.**
- **Not a problem, though it looks like one: `settings-fields` and
  `settings-arrays` pass only 3 assertions each.** Both sweeps check
  hundreds of fields, but a field is reported through `Check(fFalse,
  ...)`, which runs only when that field fails to survive. A clean run
  therefore counts just the set-up checks: the writer wrote a file, and
  the file loads back. A small count from either one is what "every field
  survived" looks like, not a sign the sweep was skipped.

### Plan item 4 -- K1: `forced-positions` renames macro 1 and leaves it

**The change.** `TestForcedPositionsQt()` saves macro 1's name into a
`QByteArray` before its `-WM 1 "AstrologQtSuiteMacro"` and replays it at
the end as `-WM 1 "<saved>"` -- the idiom `TestInterfaceSettingsQt()`
already used for the same experiment.

**Gotcha: the restore gets its own `cchSzLine` buffer.** The group's
`szLine` is `cchSzMax` (255), and the restore command is the saved name
plus eight characters, so a long macro name read back from a user's
settings would have been cut off -- and with it the closing quote, which
CLAUDE.md records as the way a truncated string setting turns its tail
into a switch.

**Not resolved: a name containing a double quote.** The replay quotes the
saved name as it is, so a macro named `say "hi"` comes back mangled.
`TestInterfaceSettingsQt()`'s restore has the same limit, and the writer
(`FOutputSettings()`) is the thing that decides how such a name is
spelled in a file, so the right fix is to restore through whatever escape
the writer uses, not to invent one here. Neither `nrvate.as` nor the
bundled settings carry such a name.

**Falsified with the canary, alone.** Before the change,
`ASTROLOG_QT_TESTS=forced-positions` with the canary on reports
`macro name 1 "" -> "AstrologQtSuiteMacro"` (9 passed). In the full suite
the same line read `"Default Planets" -> ...`. No earlier group touches
that slot (the canary baseline has no other macro line); the name is the
maintainer's own, from `nrvate.as`, which `run-qt-tests.sh` loads with
`-i` when given no arguments. So in the run the hard rule prescribes, the
leak replaced a real user setting for the rest of the process, not an
empty slot.

**After the change.** The same solo run: `[canary: 0 changes left behind
by 0 groups]`, still 9 passed. The full suite, canary on: `PASS: 5198
passed, 0 failed`, and its canary lines against item 3b's run (the
clock-driven `gs.rRot`/`gs.rTilt` filtered, and the stderr fragments
stripped) lose exactly one line -- the macro name -- and gain none.

### Plan item 5 -- K2: `divergences` sets every "None" combo to "Rainbow"

**K2's leak does not reproduce, and the reason is in the store, not the
test.** The review predicted that the decoration fill combo would take
"Rainbow" by prefix match ("Rainbow RYB") and leave `gs.nDecaFill`
changed. Reading the Graphics Settings store (`qtdialog.cpp`, after the
dialog's OK): the wheel corner, decoration fill and city colour combos
are all matched with `FEqSzI()` -- **exact**, case-insensitive -- since
the fix `TestComboPickQt()` pins ("a list with no prefix pairs is
unaffected"). "Rainbow" matches no entry of the corner or fill tables;
the store then falls back to index 0, and index 0 of both tables is
"None". So a combo that showed "None" stores "None" again. The prefix
premise was true of an older store, not of this tree.

**Measured, not only read.** `ASTROLOG_QT_TESTS=divergences` with the
canary and `-i nrvate.as`, before the change: the only line is
`gs.yWin 1558 -> 1529` (N-C), no `nDecaFill`, no `nDecaType`. The full-suite
canary baseline agrees (only the clock-driven globe angles).

**Changed anyway, as a robustness fix rather than a leak fix.** The
group's claim is about one control, and it depended on a second,
unrelated fact -- the fallback index being "None" in two other tables --
to be harmless. It now finds `dcGr_XL` by object name, as
`TestComboPickQt()` does, and touches nothing else.

**Falsified: the by-id lookup still reaches the control the checks are
about.** Built with the id deliberately misspelled (`dcGr_XLsabotage`),
`divergences` alone fails both city checks -- "picking a city colouring
left gs.fLabelAsp clear" and "city colouring scheme is 1, wanted 5" (3
passed, 2 failed). With the id restored by reversing that exact string, 5
passed. A lookup that silently found nothing would otherwise have read
as a pass on a dialog nobody touched.

**Suite.** `PASS: 5198 passed, 0 failed`, and the canary lines identical
to item 4's run (clock-driven globe angles filtered).

### Plan item 6 -- K3: `star-links` saves the star lists into 1020 bytes

**The change.** `TestStarLinksQt()` saved `gs.szStarsLin` and
`gs.szStarsLnk` by `sprintf2()` into two `cchSzLine` stack buffers and put
them back through `FProcessYXU()`. It now copies both into `QByteArray`s
(content, not pointers -- the reason the group's own comment gives for not
saving the pointer still stands) and restores from those. The empty-list
case needs no branch any more: `SzSet()` turns a NULL list into "", which
is what the old `else` wrote.

**Why it could be made to fail at all.** K3 was latent: `nrvate.as`
carries `-YXU "" ""`, so no run anyone makes by default has a list to
lose. The falsification needed a long list from outside, and that is
possible only because nothing upstream of the group caps it -- checked
before relying on it: the `-YXU` handler passes both arguments straight to
`FProcessYXU()`, which allocates to `CchSz()` and validates nothing.

**Falsified.** `ASTROLOG_QT_TESTS=star-links`, the canary on, with
`-YXU` given a 1979-character star list and a 1439-character link list
after `-i nrvate.as` on the command line. Before the change:

```
[canary star-links: gs.szStarsLin length 1979 -> 1019]
[canary star-links: gs.szStarsLnk length 1439 -> 1019]
PASS: 8 passed, 0 failed
```

-- both cut at 1019 characters, and the group itself passing, which is
the point: nothing in the suite noticed.

**After the change.** The same run: `[canary: 0 changes left behind by
0 groups]`, still 8 passed -- both lists come back at their full length.
The full suite, canary on: `PASS: 5198 passed, 0 failed`, canary lines
identical to item 5's run.

**Not resolved: the suite still does not *assert* this.** The
falsification is a canary line under a hand-built command line, not a
`Check()`; a regression back to a fixed buffer would pass the suite
exactly as before, because no run carries a long list. A standing net
would be the `settings-strings` sweep poisoning the star lists with its
300-character marker across `star-links` -- but that is a change to the
sweep's design, not to this group, and is left for the maintainer to
want.

### Plan item 7 -- K4/K5: the chart list is emptied and refilled, and only its length comes back

**How to get a chart list into a test run at all.** Every run anyone makes
starts with `is.cci == 0`, which is why K4 and K5 have never failed: a
settings file loaded with `-i` does not append to the list. Three things
do -- an AAF file, a Quick*Chart file, and Open Charts in Folder. Read
before relying on it: `-i <file>` goes to `FInputData()`, which sends a
file whose first character is `#` to `FProcessAAFFile()`, and that appends
**every** record in the file. So a three-record AAF file on the command
line after `-i nrvate.as` starts the suite with three charts in the list.
The records copy the `#B93:` line the suite's own parser test already
proves loads. Kept at `$CLAUDE_JOB_DIR/tmp/charts/preload.aaf` for the
duration.

**K5, falsified first because it needs no new instrument.** `chart-list`
alone with the three preloaded:

```
FAIL  an expression that keeps everything keeps 3 (got 6)
FAIL: 10 passed, 1 failed
```

The group appends three charts to whatever is there, then asserts the
dialog shows exactly three rows. Its own `is.cci >= cciSav + 3` two lines
earlier had it right; the row check did not.

**K4 needed the canary to look inside the list.** The same run's canary
reported nothing about the chart list, because every one of these groups
puts `is.cci` back -- the length is all it restored, and the length was
all the canary compared. So item 7 extends the canary first (it is the
instrument, and it goes in the same commit): after each group it compares
each entry the before and after lists share -- date, time, zone,
coordinates, and the name and location by content -- names the first three
that differ and counts the rest.

**String ownership, checked before designing the save.** A `CI` holds
`nam` and `loc` as pointers, and `FAppendCIList()` copies the struct
shallowly. Saving the entries and writing them back is only safe if
nothing in between frees those strings. Read: `FilterCIList()` compacts
the array and frees nothing; the chart list dialog's "Delete Chart"
shifts entries down with `CopyRgb` and frees nothing; `FAppendCIList()`
reallocates `is.rgci` when it grows, but copies every entry across and
never shrinks the allocation, so a saved count always fits back. A
shallow copy of the entries is therefore enough.

**The change.** `ChartListPinQt` -- a `QVector<CI>` of every entry, taken
when it is constructed, and a `Restore()` that writes them back and sets
the count. It replaces the count-only save in `export-roundtrip`,
`null-names`, `chart-list` and `open-dir`, and the first-eight-entries save
in the four oracle legs, which lost everything past the eighth chart. The
four oracle `cciSav` declarations had nothing left to read them and were
removed with it -- they would have been item 3's gate failing on this
commit. `chart-list` also empties the list explicitly after taking its pin
(K5), and its "three charts went into the list" check is now exact.

**Deliberately left alone: `TestFileParsersQt()`.** It also saves
`is.cci` and puts it back, which reads like a thirteenth site. It is not:
it never empties the list, only loads parser files that append past the
end, so restoring the count trims exactly what it added and the entries
before it are untouched. A pin there would be correct and prove nothing.

**Falsified, K4, with the extended canary** (built before the fix, so the
instrument is the only thing that changed). Each group alone with the
three preloaded charts:

- `export-roundtrip`: `is.rgci[0] "PreloadChart1" 3/4/1981 -> "Probe Name"`
- `null-names`: `is.rgci[0] "PreloadChart1" 3/4/1981 -> ""`
- `chart-list`: entries 0, 1 and 2 all overwritten, plus K5's failure
- `open-dir`: entries 0, 1 and 2 -> "Default", "One", "Three"
- `oracle`: nothing -- its four legs saved the first eight entries, and
  three fit. (A ninth chart would have been lost; not measured, read.)

And the whole suite started that way ended `FAIL: 5197 passed, 1 failed`,
the canary following the list from group to group -- `null-names`
overwriting what `export-roundtrip` had already overwritten, and so on --
while every group's own count came back.

**After the change, alone with the preload:** `export-roundtrip` 18,
`null-names` 3, `chart-list` 11, `open-dir` 5, `oracle` 576 passed, and
not one chart-list canary line among them. `chart-list` passes the check
that failed.

**The whole suite with the preload, after:** `PASS: 5198 passed, 0 failed`
-- the count a run with an empty list gets -- and no chart-list canary
line anywhere in it. Before: 5197 and one failure, with the list rewritten
four groups in a row.

**And the default run, no preload:** `PASS: 5198 passed, 0 failed`, canary
lines identical to item 6's. With the list empty on entry the pin saves
nothing and restores nothing, so no ordinary run can tell the change is
there -- which is the whole of why K4 and K5 lasted.

**Not resolved: no standing net.** As with item 6, the falsification is a
hand-built command line. `run-qt-tests.sh` never starts with a chart list,
so a regression to a count-only save passes the suite. Giving the suite a
preloaded run of its own would close it; that changes what `make check`
runs and how long it takes, so it is left for the maintainer.

### Plan item 8 -- V1: `color-scheme` unsets `ASTROLOG_QT_THEME` for good

**The canary could not see this, so it was taught to first.** An
environment variable is process state no field table names. The canary
now records whether `ASTROLOG_QT_THEME` is set and its value, before and
after each group -- set-to-empty and unset kept apart, because
`NDarkPreferenceQt()` treats them differently -- and prints
`ASTROLOG_QT_THEME "dark" -> unset` when a group changes it.

**Falsified before the change.** `color-scheme` alone, with the canary:

- run under `ASTROLOG_QT_THEME=dark`:
  `[canary color-scheme: ASTROLOG_QT_THEME "dark" -> unset]`, 47 passed;
- run with it unset: no canary line, 47 passed.

So a developer checking the dark scheme with the variable, as CLAUDE.md
says to, got the groups before `color-scheme` dark and every group after
it under the saved preference instead, with nothing saying so.

**The change.** The group saves whether the variable is set and its value
next to the theme preference it already saved, and puts the variable back
at the end of the block.

**Gotcha: where the restore goes is not free.** The obvious place is the
existing `qunsetenv()` straight after the two "the env var outranks a
saved theme" checks. It would be wrong there. Every check after that line
needs the variable unset: "auto" has to fall through to detection, an
empty preference must not read as a choice, and "choosing Dark actually
darkens the palette" would pass on the variable alone. So the restore
goes last, just before the saved preference is re-applied -- which also
means the closing `ApplyColorSchemeQt()` sees the environment the group
started with.

**Checked, because it is the shape of a bug this project already paid
for:** the canary's new line prints `qgetenv("ASTROLOG_QT_THEME").constData()`
-- a pointer into a temporary `QByteArray`. CLAUDE.md records a
read-after-free from exactly `constData()` on a temporary, invisible on
Linux and a SIGSEGV on macOS. That one bound the pointer to a variable
that outlived the temporary. Here the temporary is an argument inside the
`CANARY()` macro's single comma expression, so it lives until the end of
that full expression, which is after the `printf()` that reads it. Safe
as written; it would stop being safe the moment someone hoists the
`constData()` into a local, and the line is left un-hoisted on purpose.

**After the change, alone:** under `ASTROLOG_QT_THEME=dark`, `[canary: 0
changes left behind by 0 groups]`, 47 passed; with it unset, the same. So
the saved-theme pair, "auto" and the repaint checks still run with the
variable cleared inside the group -- the restore did not land early.

**The full suite, variable unset:** `PASS: 5198 passed, 0 failed`, canary
lines identical to item 7's run -- as expected, since with nothing to put
back the restore does nothing.

**The full suite under `ASTROLOG_QT_THEME=dark` -- the run V1 is about:**
`PASS: 5198 passed, 0 failed`, and no `ASTROLOG_QT_THEME` canary line in
it, so the variable held for every group after `color-scheme`. Before the
change the same run would have switched to the saved preference a fifth
of the way in. The suite has no failure under a forced dark theme, which
nobody had measured before.

### Plan item 9 -- O2, O5, O6: the oracle's degenerate-house loop and two stale numbers

**O2, the change.** Leg 4b walks `rgDegen[]` -- the house systems expected
to collapse toward the pole -- in two loops, both written `iDeg < 1`. The
set had shrunk from seven entries to one and the bound was edited to
match; the next entry added would have been silently ignored by both.
Both loops are bounded by `sizeof(rgDegen)/sizeof(*rgDegen)` now.

**O2, why it needs a falsification even though nothing changes today.**
With one entry, `< 1` and the `sizeof` bound run the same single pass,
so the suite cannot tell them apart, and "the count did not move" says
nothing about whether the new bound reads a second entry. So it was
checked the one way that can see it: build with a second entry added to
`rgDegen[]` and require the leg to notice.

**O5.** The failure message for the "still collapses" check read "(%d
appear changed fixed -- if that is deliberate, drop them from rgDegen)",
two drafts spliced together. It now says what the number is: how many
systems expected to collapse no longer do.

**O6.** The eclipse leg's comment said "59 midpoints" and, further down,
"the one disagreement in 159 checks", while the assertion requires 55
gaps. The assertion is right and both comments were stale: `jdPrev` is
reset at the start of each of the five epochs, so each epoch's twelve
eclipses give eleven gaps, 5 x 11 = 55, and 60 + 55 + 40 = 155. Checked
from the loop, not taken from the assertion.

**Falsified.** The oracle alone with the change: 576 passed. Built with
`rgDegen[] = { hsSineDelta, hsPlacidus }` -- Placidus added, which does not
collapse at these latitudes:

```
FAIL  and Pullen (S.Delta) still collapses, as its author wrote (1 expected
      to collapse no longer do -- if that is a deliberate fix, drop them
      from rgDegen)
FAIL: 575 passed, 1 failed
```

So the bound now reads the second entry -- the old `< 1` would have
passed that build -- and the same run shows the O5 message reading as a
sentence. With the entry removed again (the exact string reversed, not a
checkout), 576 passed.

**Suite.** `PASS: 5198 passed, 0 failed`, canary lines identical to item
8's run. Nothing here changes what runs: one-entry loops, a message only
printed on failure, and two comments.
