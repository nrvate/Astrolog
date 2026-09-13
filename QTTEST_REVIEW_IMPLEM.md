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

### Plan item 11 -- R1: the chart-render group judges "blank" against the corner pixel

**The review was right about the comparison and wrong about the size of
it.** R1 said the corner holds the border, that comparing against it
"would misreport a monochrome run", and that the group passes only
because it runs in colour. Measured on the renders the group makes
(`GraphicsChartCaptureQt()`, which is "that loop plus a save" through the
same `SetChartModeQt()`), in colour *and* monochrome:

- the corner is the border -- grey `(191,191,191)` in colour, white in
  monochrome -- and the background is black;
- so against the corner nearly **every** sample counts as drawn: orbit
  scored about 174,000 against the corner and 939 against the background.

The consequence is not a false failure in monochrome. It is that the
check **cannot report blank at all**, in any run. A chart that draws
nothing still differs from its border everywhere.

**Measured with the instrument's own blind spot in mind.** Two traps on
the way, both caught by measuring a second way:

- *"2 distinct colours" as proof a run was monochrome* -- checked by
  counting the colour run too: 6 to 14 colours per chart against 2, so
  `-Xm` did take.
- *Row bands of "non-background" pixels* came back as one band covering
  every row, because the border's two side columns cross every row.
  Re-measured with 16 pixels cut from each side: exo's only content is
  rows 1544-1553 (the footer line), horizon's header is rows 3-12.

**What is actually drawn inside the border.** Samples (every fourth
pixel) differing from the background inside a margin, colour run:

| margin | exo | orbit | calendar | horizon | border+text alone |
|---|---|---|---|---|---|
| 8 px | 71 | 130 | 117 | 202 | ~800-900 |
| 16 px | **0** | 59 | 117 | 122 | -- |

So the border and the header and footer lines alone are 800-900 samples.
Comparing against `gi.kiOff` -- the fix R1 proposed -- with the group's
threshold of 100 would still pass a chart that drew nothing but its frame.
The margin is what makes "blank" measurable.

**And exo really is blank: it is not a graphics chart.** Viewed: its
render is the border and the footer, nothing else. `DrawChartX()` has no
`case gExo`; the Qt menu adds it through `AddChartModeTextAction()`, like
Aspect List and Arabic Parts; and **Windows sets `us.fGraphics = fFalse`
for `cmdChartExo`**, as it does for `cmdChartAspect` and
`cmdChartArabic` (`wdriver.cpp`). The render list in `TestChartRenderQt()`
holds all three -- `gAspect`, `gArabic`, `gExo` -- and has passed "renders
non-blank" on all three for as long as the corner check has existed.

**A correction to the record: QT_GUI_PLAN.md work log item 24.** That item
found Aspect List and Arabic Parts drawing an empty window, and says "A
pixel-blankness assertion did not catch this (something still paints
offscreen)". Nothing was painting offscreen. The assertion compared
against the border, so it could not see a blank chart. The item's fix --
asserting `us.fGraphics` rather than pixels -- was the right one, and the
reason it gave for needing it was not. The work log is left as written;
this is where the correction lives.

**Found here, not fixed (not in the plan, each needs its own net):**

- **N-F (P2) the menu-firing group's "chart went blank" check has the same
  blind spot, one layer down.** It compares against `gi.kiOff`, as R1
  recommends, but over the whole image with a threshold of 20 -- and the
  border and text lines alone are 800-900 samples. It cannot report a
  blank chart either.
- **N-G (P3) `CpixDifferQt()`, used by `clear-screen`, compares against
  `pixel(0,0)`.** R1 hoped it might be the shared helper; it has the
  corner comparison too. Whether it is blind there depends on whether a
  cleared screen draws a border, which was not measured.
- **N-H (P3) `GraphicsChartCaptureQt()` captures `gExo`**, while its own
  comment says Aspect and Arabic are left out "on purpose: DrawChartX()
  has no case for either". Exo is the same case.

**The change.** `CpixDrawnQt(nMargin)` counts samples inside a margin that
differ from `gi.kiOff`; `TestChartRenderQt()` uses it at 16 pixels with a
threshold of more than 20 (the sparsest real chart measured is 59). And
`gAspect`, `gArabic` and `gExo` leave the group's render list, with a
comment saying why: they are text charts on both builds, and the
menu-firing group already asserts the invariant that matters for them
(that choosing one switches graphics off).

**Falsified, with no sabotage needed -- the tree already held the blank
renders.** Built with the new check and the list *unchanged*, the group
alone:

```
FAIL  Aspect: rendered blank (0 samples inside the border differ from the background)
FAIL  Arabic: rendered blank (0 samples inside the border differ from the background)
FAIL  Exo: rendered blank (0 samples inside the border differ from the background)
FAIL: 134 passed, 3 failed
```

-- the same three and only those, in colour and in monochrome (`-Xm`),
where the corner check had passed all 26 (137 passed). So the new check
fails exactly the charts that draw nothing, and passes every chart that
draws something, including orbit at 59.

With the three removed: 125 passed, 0 failed, in colour and in
monochrome. The 12 fewer assertions are the three modes' four checks
each.

**Documents that stated the old count.** CLAUDE.md ("26 chart types
render non-blank") and QT_TESTING.md (twice) describe the suite as it is,
so they say 23 now. QT_GUI_PLAN.md and REFACTORING.md also say 26, in work
log entries and campaign records that describe the suite as it was when
they were written; those are left alone.

**Gotcha: `_Xm` on the command line does not do what it does in a
settings file.** Tried first for a monochrome run, it printed nothing
but `nrvate.as`'s own `-YRd` range warning and exited 0 without running a
group -- that warning appears in every run, so it was not the cause.
`FProcessSwitches()` does read a leading `_` as "and"; why the command
line then stops was not traced. `-Xm`, which toggles, gave the monochrome
run instead. Noted so nobody reads a silent exit 0 as a passing run.

**The full suite:** `PASS: 5186 passed, 0 failed` -- 12 fewer than 5198,
exactly the three removed modes' four checks each -- and canary lines
identical to item 9's run. **The suite's count moves here, on purpose.**
Every count in this document after this item is against 5186.

### Plan item 10 -- W1: the settings-strings markers are too short to catch what the header says they catch

(Done after item 11, not before: both needed the test binary to
themselves for a sabotaged build, and item 11's runs were already under
way. The commit order says 11 then 10; the plan's order is unchanged.)

**What the review said.** `TestSettingsStringsQt()`'s header: "The marker
is over cchSzMax again, because -YD and -YU formatted their value through
sz until this group was written." Only the macro markers were long (300
characters, through `SzSetFieldMarkQt()`). The object-name marker was
`"%.3sProbe%d"` and the star marker `"StarProbe%d"` -- nine to twelve
characters -- so the regression the header names could not be caught.

**Checked before relying on "they can be made long".** Neither switch caps
its value: `-YD` stores through `SetObjDisp()` and `-YU` through
`FCloneSz()`, both of the whole argument, and `NParseSz()`'s `cchSzMax`
copy applies only to the *object* argument, not the name. The writer
(`FOutputSettings()`) prints both values in pieces with `PrintF()`, and
the reader already round-trips the 300-character macro markers through
the same settings file, so a long value survives the reader too.

**Falsified before the change: the short markers pass a broken writer.**
`io.cpp` was changed, for the length of the check only, to the shape the
header describes -- one `sprintf2()` through `sz` for the first `-YD`
loop and for `-YU` (each target string confirmed to occur exactly once
before replacing it). `settings-strings` alone, with the markers as they
were:

```
PASS: 3 passed, 0 failed
```

So the group certified a writer that cuts every renamed object and
custom star at 255 characters.

**The change.** `SzStringMarkQt(szHead, i, ...)` builds `"<head>Probe<i>-"`
and pads it with `x` to 300 characters, as `SzSetFieldMarkQt()` does for
the macros; the object markers keep their three-letter object head so
every element stays distinct. Used where the markers are set and where
they are checked, so the two cannot drift apart.

**Falsified after the change: the long markers fail the same broken
writer.** Rebuilt with the markers lengthened and `io.cpp` still
sabotaged, the group alone:

```
FAIL  and the file it wrote loads back with every string emptied
FAIL  szObjDisp[0] (-YD) did not survive a save and reload
FAIL  szStarCustom[1] (-YU) did not survive a save and reload
FAIL  is.rgszMacro[0] (-M0) did not survive a save and reload
FAIL: 2 passed, 4 failed
```

The last two lines are worth reading closely, because they are CLAUDE.md's
warning happening in front of us: a `-YD` value cut at 255 characters
loses its closing quote, so the *rest of the file* is read wrongly -- the
reload itself reports failure, and the macros, whose writer was not
touched, are lost with it. A truncated string setting does not only lose
its tail.

With `io.cpp` put back (the same two exact strings reversed; `git diff`
then showed `io.cpp` untouched), 3 passed, 0 failed.

**Suite.** `PASS: 5186 passed, 0 failed`, canary lines identical to item
11's run. The group's assertion count does not move -- per-element
failures report through `Check(fFalse, ...)` only -- so the whole of the
change is in what those checks can now see.

### Plan item 13 -- S3: `ok-settles` restores the chart info by struct copy -- closed, no change

**What S3 said.** `TestOkSettlesQt()` ends `ciMain = ciSav; ciCore =
ciCoreSav;`, a copy taken before 25 dialog OKs, "one of which (Set Chart
Info) `FCloneSz()`es `ciCore.nam` and `.loc`" -- so the copy's pointers
are either freed or, when the new text fits, overwritten in place by
`FCloneSzCore()`. The plan's answer was a `CIPin` of the eight numeric
fields.

**Killed against the code, in three reads:**

1. **`FCloneSzCore()` does behave as S3 says** (general.cpp): it copies in
   place when the old buffer is large enough, and otherwise frees it. So
   the premise about the function is right; the question is whether
   anything calls it on a chart's name.
2. **Nothing does.** Both chart info dialogs (`ShowChartInfoForQt()` and
   the default-chart one) build a local `CI` with **fresh `SzClone()`
   copies** and assign the struct (`*pci = ci`, `ciDefa = ci`), and their
   own comment says why: "FCloneSz FREES the old pointer, so editing a
   name here pulled it out from under every other CI still holding it".
   The settings replay the group runs goes through `-zi` and `-qb`, which
   likewise assign fresh `SzClone()` copies (switch.cpp).
3. **Nothing frees a chart's name or location anywhere in the tree.** A
   search for `DeallocateP`/`DeallocatePIf` on a `nam` or `loc` found one
   hit, `express.cpp`'s `DeallocateP(szAlloc)` -- an expression list
   buffer that matched the pattern by its name.

So a `CI` copied before the OKs holds pointers that are still allocated
and whose text nobody has touched. The struct restore is correct as
written, and replacing it with eight numeric fields would have left the
name and location wherever the last OK put them -- a worse restore.

S3 was a real hazard in an earlier tree -- the dialogs' comment records
exactly that bug being fixed -- and a stale finding in this one. Closed
here with that evidence, per CLAUDE.md's "closed means the killing
evidence is written down where the finding was raised, so it is never
re-flagged".

**S4, also in Phase 3's header, was already closed by item 7.**
`TestNullNamesQt()` and `TestExportRoundTripQt()` empty the chart list
and append; `ChartListPinQt` now saves and restores every entry in both.

### Plan item 14 -- S1: `midpoint-glyph` restores `us` and `gs` by whole-struct copy

**What S1 said.** `TestMidpointGlyphQt()` does `US usSav = us; GS gsSav =
gs; ... us = usSav; gs = gsSav;` -- the pattern this file's own rules
forbid, because a whole-struct restore puts every `char *` member back to
the pointer it held, and anything in between that reallocates one leaves
the restore pointing at freed memory. "Safe by luck"; the group pins about
ten fields by hand and could restore those instead.

**Checked before relying on either half.**

- *Is the hazard live?* The group calls `SetChartModeQt()`, `RedrawQt()`
  and `CastChart()` between the copy and the restore. `SetChartModeQt()`
  (qtdriver.cpp) contains no `FCloneSz`, `SzClone`, `DeallocateP` or
  `us.sz`/`gs.sz` at all. So S1 is right that nothing reallocates a string
  today -- it is a latent hazard, not a current fault, and the change is
  a hardening.
- *Would "restore the ten fields" be enough?* No, and this is the part the
  review could not have seen by reading. The group's hash lambda calls
  `SetChartModeQt(gWheel)`, which rewrites the chart-type flags; restoring
  `gi.nMode` does not put those back (the same shape as N-B and N-D).
  Measured: with the struct restore removed for a measurement build (and
  put back by reversing the exact string), the group alone left

  ```
  [canary midpoint-glyph: us.fListing 0 -> 1]
  [canary midpoint-glyph: gs.fText 1 -> 0]
  ```

  -- one of the ten, and one chart-type flag that is not among them. The
  other nine of the ten showed nothing only because a solo run already
  holds the values the group sets; after `TestAllMenuActionsQt()` in the
  full suite they do not, so all ten are kept.

**The change.** The whole-struct copies are gone. The ten fields the group
sets are saved and restored by name, and the chart-type flag block
(`us.fListing` up to `us.fVelocity`) is saved and restored as bytes -- the
same range and idiom `TestSortStarQt()`'s A-4 leg already uses around
`InitVariables()` -- so every flag `SetChartModeQt()` can touch comes back,
whichever mode the group was entered in. `ciCore` keeps its struct copy:
S3's analysis (item 13) applies, since nothing frees a chart's strings.

**Falsified: the canary sees each pin.** The group alone with the pins:
`[canary: 0 changes left behind by 0 groups]`, 4 passed. Rebuilt with the
`gs.fText` restore left out on purpose:

```
[canary midpoint-glyph: gs.fText 1 -> 0]
```

Put back by reversing that exact string (`git diff --stat` back to the
intended change), 0 changes again. So the restore is doing the work, and
the canary would name the next field somebody adds to the group without
restoring.

**Gotcha, caught in review of this very item: the first version of the
new comment said "the other eight".** Ten fields are pinned and one of
them leaked (`gs.fText`); `us.fListing` is not one of the ten. So nine
showed nothing. Corrected before committing -- the kind of wrong number
that CLAUDE.md records three documents once disagreeing over.

**Suite.** `PASS: 5186 passed, 0 failed`, canary lines identical to item 10's run. The
struct copies restored more than the group changed, and the pins restore
exactly what it changes, so a full run cannot tell them apart -- the
canary runs above are the evidence.

### Plan item 15 -- S2 and K6: the output file names are cloned over and dropped

**What the review said.** S2: `export-roundtrip` and `window-size` do
`FCloneSz(szPath, &is.szFileOut)` and later put back a *saved pointer*.
`FCloneSzCore()` copies into the old buffer when the new text fits, so a
run started with `-o` gets its name overwritten by the scratch path; when
it does not fit, the old buffer is freed and the restored pointer
dangles. K6: `shared-core`, `line-drawing` and `long-strings` clone their
scratch name into `is.szFileScreen` and finish with `FCloneSz(NULL, ...)`,
so a run started with `-os` loses it. The plan said: assign the pointer,
as `FSaveSettingsToQt()` does.

**The canary was taught to see it first.** It now records both names by
content and by whether they are set at all, and names a change.

**Falsified before the change,** each group alone:

- `export-roundtrip` and `window-size`, started with `-o` and a 140-character
  path (long enough that the scratch path is copied in place rather than
  reallocated, so the result is deterministic instead of a read of freed
  memory): `is.szFileOut` changed in both; in `window-size` the user's
  name now read as the group's `/tmp/...` scratch path.
- `shared-core`, `line-drawing`, `long-strings`, started with `-os`:
  `is.szFileScreen "<path>" -> "(NULL)"` in all three.

**Why the fix is not the plan's pointer assignment.** Before writing it, a
search for every write to these two fields outside the suite:

| Where | What |
|---|---|
| `InitVariables()` (astrolog.cpp) | `FCloneSz(NULL, &is.szFileScreen)` -- frees it |
| `CaptureTextToFileQt()` (qtdriver.cpp) | clones into `is.szFileScreen`, then frees it |
| `FExportChartQt()`, `StrDefaultSuffixTestQt()` (qtdialog.cpp) | clone into `is.szFileOut` |
| `-o`, `-os`, `>` (`NSwoCore()`, switch.cpp) | clone into both |
| `FinalizeProgram()` | frees both at exit |

A stack buffer's address left in either field while any of those runs is
freed or written through. Assigning the pointer is safe only as long as
nothing in the group's window reaches one of them -- the same "safe by
luck" S1 was about. So both halves are saved **by content** instead: a
`QByteArray` of the value and a flag for whether it was set at all. The
scratch name still goes in through `FCloneSz()`, and the restore is
`FCloneSz()` of the saved text, or of NULL when there was none. No stack
address ever reaches `is`, whatever runs in between.

**Checked and left alone.** Six other groups save `is.szFileOut` by pointer
and point it at a stack path by plain assignment (`is.szFileOut = szPath`).
They never clone into the user's buffer, so they cannot overwrite it, and
they put the original pointer back. They carry the latent hazard in the
table above only if their windows reach one of those functions. The review
did not flag them, and changing them would need its own measurement.

**After the change, the same five runs:**

- `export-roundtrip` with the long `-o`: `[canary: 0 changes left behind by
  0 groups]`, 18 passed; `window-size`: 0 changes, 8 passed.
- `line-drawing` and `long-strings` with `-os`: 0 changes, 7 and 37
  passed. `shared-core`: its `is.szFileScreen` line is gone, and the only
  line left is `us.fListing 0 -> 1` -- N-D, the chart-type flag
  `SetChartModeQt()` sets and nothing restores, which was there before
  this item and is not one of the file names.

**The first full suite after the change passed (`PASS: 5186 passed, 0
failed`) and its canary diff against item 14 gained two lines.** Neither
is a regression -- the canary had never watched these names before this
item, and neither group was touched by it -- and both are real:

```
[canary menu-actions: is.szFileOut "(NULL)" -> "/tmp/astrolog-qt-copy-SInaUX"]
[canary chart-export: is.szFileOut "/tmp/astrolog-qt-copy-SInaUX" -> "/tmp/astrolog-qt-bmpmode-<pid>-P" (rewritten)]
```

**`chart-export` is S2's shape at a third site the review did not list.**
It saves `is.szFileOut` as a bare pointer; its bitmap leg calls
`FExportChartToFileTestQt()`, which reaches `FExportChartQt()`, which
`FCloneSz()`es its path into `is.szFileOut` -- in place, because the name
`menu-actions` left there is long enough. The pointer comes back holding
the bitmap leg's path. Invisible whenever `is.szFileOut` is NULL on entry,
which is every run except one following `menu-actions`.

**Gotcha: the obvious fix frees a stack address.** Between the bitmap leg
and the restore, the group's format loop sets `is.szFileOut = szPath` --
a stack array. Restoring with `FCloneSz(saved, &is.szFileOut)`, as the
other five sites now do, would hand that stack address to `FCloneSzCore()`,
which frees the old buffer when the new text does not fit. So this site
restores in two steps: the saved pointer first (the heap buffer, which
drops the stack address without freeing it), then the saved text copied
into it. The five sites changed above never point the field at the stack,
which was checked before relying on the simpler restore there.

**Gotcha, paid for on this site: an exact-string edit that fails does
not stop a chained command.** The first attempt replaced `is.szFileOut =
szFileOutSav;` file-wide, and that line occurs nine times. The replace
helper's count check refused and the Python script exited before writing
anything, which is the hard rule working. But the verification ran in the
same shell command without `set -e`, so it carried on: the warning audit
passed on the unchanged file, `make` found nothing to rebuild, and a
single-group run and a full suite started against the old binary, printing
results that looked like a verification. Caught by reading the output
top-down: the traceback, and a binary timestamp that had not moved. The
suite was stopped through its task ID, and the edit was redone with both
replacements confined to `TestChartExportQt()`'s own body. Lesson for the
rest of this work: check the edit's own output, and the binary's
timestamp, before believing anything after it.

**That accident produced this site's pre-fix evidence.** The single-group
run against the old binary, started with the long `-o` and with nothing
from `menu-actions` in the way:

```
[canary chart-export: is.szFileOut "<scratch>/ooo...o.as" -> "/tm..."]
```

So `chart-export` overwrites a user's `-o` name on its own. `menu-actions`
only made it visible in the default run.

**After the scoped edit, confirmed rebuilt** (binary timestamp moved to
14:03:43, warning audit empty): `chart-export` alone with the long `-o`
reports `[canary: 0 changes left behind by 0 groups]`, 22 passed.

**The full suite, on the rebuilt binary:** `PASS: 5186 passed, 0 failed`.
Against item 14's canary run it gains exactly one line, the N-I port leak
below (`menu-actions` leaving a Copy temp file's name in `is.szFileOut`);
the `chart-export` rewrite line is gone.

## New findings, deferred

- **N-I (P2, port, not the suite) -- Copy Chart leaves a deleted temp
  file's name in `is.szFileOut`.** Deferred 2026-09-13: it is shipping
  code outside this review, and changing it needs its own net and a
  maintainer's call on the Windows split below. Found by the canary above.
  `CopyChartVectorQt()` (qtdialog.cpp) exports to a `QTemporaryFile` through
  `FExportChartQt()`, which does `FCloneSz(szFile, &is.szFileOut)` *and*
  `FCloneSz(szFile, &gi.szFileOut)` and restores neither. For Save and
  Export that matches Windows exactly -- `wdialog.cpp` clones the chosen
  name into both. For Copy it does not: Windows' `cmdCopyBitmap` through
  `cmdCopyWire` case (wdriver.cpp) clones the temp name into
  **`gi.szFileOut` only**. So after any vector Copy Chart this build carries
  a stale settings/chart output name, pointing at a file that no longer
  exists, that the Windows oracle never sets. The likely fix is for the
  copy path to leave `is.szFileOut` alone, as Windows does.

### Plan item 16 -- K7: a string literal and a static array installed into settings that get freed

**What K7 said.** `TestAspectDashQt()` assigns a string literal to
`us.szExpAsp` and `TestSharedCoreFixesQt()` assigns a static array to
`gs.szSidebar`; both put the old pointer back, and "only `-YXt` ever
`FCloneSz()`es the sidebar, so nothing frees a literal today". The plan:
`SzClone()` instead, "freeing on restore".

**Checked before relying on it.** Both fields are cloned by a switch --
`-YXt` for the sidebar (switch.cpp) and `~A` for the aspect expression,
through the AstroExpression switch table -- and `FCloneSz()` frees the old
buffer whenever the new text does not fit. So the hazard is one switch in
the window away from a crash, for both, not only the sidebar.

**Falsified before the change, and it crashes rather than fails.** The
sabotage is exactly what those two switches do: an `FCloneSz()` of a longer
value straight after each install -- a padded `"=z 90"` into `us.szExpAsp`,
4000 characters into `gs.szSidebar`. Each group alone:

```
== aspect-dash rc=134   munmap_chunk(): invalid pointer
== shared-core rc=134   free(): invalid pointer
```

Both abort the process -- glibc refusing to free a literal and a static
array -- which is why CLAUDE.md calls this "a crash with no useful
backtrace": the fault is in the suite, and it surfaces in allocator code.

**The change, and the part of the plan it does not follow.** Both installs
use `SzClone()`, so the field always holds a heap buffer `FCloneSz()` may
free. They are **not** freed on restore, against the plan's wording, for
a reason read out of `general.cpp`: `PAllocate()` adds each allocation to
`is.cAlloc`, `is.cAllocTotal` and `is.cbAllocSize`; `SzClone()` subtracts
all three again, so its strings are invisible to the leak counters by
design; `DeallocateP()` subtracts from `is.cAlloc` a second time. Freeing
an `SzClone()` string therefore drives the live-allocation count below the
truth. The file's other `SzClone()` assignments (`us.szExpDisp3`,
`us.szExpMenu`, `us.szExpListF`) are not freed either; these follow them.

**Checked: the aborts did not leave core files in the tree.** `timeout`
reported "the monitored command dumped core" for both, which reads like a
`core` file next to the binary, waiting to be staged. It is not: this
machine's `/proc/sys/kernel/core_pattern` pipes dumps to
`systemd-coredump`, so they went to the system's coredump store, and
`git status` showed only the two files being edited. On a machine whose
pattern is a plain `core`, a sabotage run like this one would leave one in
the worktree.

**After the change, with the same sabotage still in:**

```
== aspect-dash rc=0   PASS: 5 passed, 0 failed
== shared-core rc=0   PASS: 12 passed, 0 failed
```

The switch-shaped clone now frees a heap buffer, as it may. The sabotage
lines were then removed by exact string (`grep -c "K7 SABOTAGE"` reads 0,
and the diff is back to the intended 11 lines), the warning audit is
empty, and the two groups alone pass 5 and 12 again.

**Suite.** `PASS: 5186 passed, 0 failed`, canary lines identical to item
15's run. A heap copy in place of a literal changes nothing a run can see
until something clones over it -- which is the point.

### Plan item 17 -- the comment pass: displaced comment blocks moved to their functions

**No code change, and proven so.** One commit that moves comments only.
The review counted "about 30" displaced blocks; the move is **31**, because
several of the review's entries were two or three headers for different
functions run together with no separator between them -- the dark-mode and
application-icon pair, the credits box and the window-size limits, forced
positions with the shared-core bugs and relationship mode, three chart-list
headers above `NExpEvalQt()`, and the Animation block with Clear Screen's
under it. Each was split at the line where its own first sentence starts.

**Found by text, not by line number.** Items 14 to 16 had inserted lines
above most of these blocks, so the review's line numbers pointed into
unrelated code by then -- a dump taken by number between items came back
with code lines where comments should be. The move script
(`$CLAUDE_JOB_DIR/tmp/move_comments.py`, not committed) names each block by
its exact first and last line and each target by its exact signature, and
stops if any of those matches other than once, if a block contains a
non-comment line, or if two blocks overlap. A dry run resolved all 31
before anything was written.

**Checked after writing, three ways:**

- **Every non-blank line is still there, and nothing else is:** the multiset
  of non-blank lines before and after differs by exactly one added `//`, the
  separator joining work log item 115's paragraph to the comment that
  already sat above `TestLongStringsQt()`. So no code line moved, changed or
  vanished; 14 blank lines went, each one of a pair a cut block left behind.
- **Each block is where it belongs:** for all 31, walking down from the
  block's first line through comment lines lands exactly on its target's
  signature. 0 placement failures.
- **It still compiles clean and behaves the same:** warning audit empty;
  suite below.

**Seven of the 31 were one run in reverse order** -- combo picks, field
parse, orb grids, screen colours, notice, chart scroll, and key help (which
was right) -- which is what a pass that reordered functions without their
leading comments leaves, as the review guessed.

**The blank-line side effect, checked.** The move collapses a run of blank
lines to at most two, so no cut leaves a hole. That rule also tidied the
two runs of three blank lines that were already in the file, away from any
cut. Checked that the collapse did not go too far: no function's closing
`}` is now followed directly by a comment or by the next function (0 before,
0 after).

**The duplicated paragraphs, in the same pass as the plan asks (H2, D1,
E3).** Each was read in the current tree first:

- **H2:** two consecutive paragraphs near the top both said the bundled
  `ephem/` and the body list are one set. Merged into one that keeps the
  first's facts (every `rgObjSel[]` row has its file, every asteroid file is
  a row) and the second's point (a run against `ephem/` and one against
  `/swe` assert the same number).
- **D1:** the line "The generated git sha the About dialog shows; see
  qtdialog.cpp." restated the long About comment directly above it, and is
  gone. The review's structural fix -- move `DriveModalQt()` up and drop its
  forward declaration -- is a code move, which a comment-only commit cannot
  carry. **Not done here**; the forward declaration and its explanatory
  comment stay.
- **E3:** of two consecutive paragraphs both opening "A check that examined
  nothing would pass too", the first was the draft of the second and is
  gone.

A line-by-line comparison before and after these three: 13 comment lines
removed, 5 added, and **no non-comment line changed**. Warning audit empty.

**Suite.** `PASS: 5186 passed, 0 failed`, canary lines identical to item
16's run. That run used a binary built from the moved comments *before* the
H2/D1/E3 edits; those three changed comment lines only (checked line by
line above, and the file compiled clean after them), so the result stands
for the committed source without a second 3-minute run.

### Plan item 18 -- one button helper for the dialog drivers

**The review's counts were low, and the loops were not all one thing.** C1
counted 23 "OK loops" and 16 Cancel variants. A classification of every
button loop in the current tree found 32 matching "OK" by label and 10
matching "Cancel", in seven shapes -- and the shapes do not all do the same
thing:

| Shape | Sites | What it does |
|---|---|---|
| click first match, return (one line) | 18 | the common one |
| click first match, return (braced) | 6 | same, incl. `ClickOkInModalQt()` |
| click every match, keep looping (two-line) | 5 | clicks all "OK"s |
| click every match, keep looping (one line) | 2 | same |
| remember the button, click later (two-line) | 4 | fields set in between |
| remember the button, click later (one line) | 2 | same |
| anything else | 5 | left hand-written, below |

**The change.** Two helpers after `Group()`: `PpbButtonQt(pw, szText)`
returns the first push button labelled exactly `szText`, and
`FClickButtonQt(pw, szText)` clicks it and says whether there was one. The
37 loops of the first six shapes are replaced by a script whose six
patterns each had to match exactly their expected count, or nothing was
written. The "remember" shapes become `ppbOK = PpbButtonQt(pw, "OK");`.

**Behaviour change, deliberate and bounded:** the seven "click every match"
loops now click the first match only. They differ only on a dialog with
two identically labelled buttons, which none of these dialogs has; the
review (G2) had already called the difference harmless.

**Left hand-written, and why.** The five loops still matching a button by
label each do more than find one button:

- `TestAboutVersionQt()` breaks out after OK and reads the dialog after;
- `ClickInModalQt()` finds a named button *and* OK in the same pass;
- `DriveObjSelQt()` (two loops) walks an indexed button list and handles
  OK refusing an unparseable row;
- `TestObjSelLookupQt()` collects several controls and Cancel in one pass.

**Not done: standardising on object names (`IDOK`/`IDCANCEL`).** The plan
asked for it where it exists. Eight sites already look buttons up that way.
But a label lookup works on every dialog, and an object-name lookup only on
the ones built from `astrolog.rc`; moving each site over needs that checked
dialog by dialog, which is its own change. The helpers make it one edit
later.

**Falsified: the call sites go through the helper.** Four dialog-driving
groups alone, with the helpers in: `graphics-fields` 13, `field-parse` 6,
`orb-grid` 7, `chart-list` 11 passed -- the same counts as before the
change. Rebuilt with `FClickButtonQt()` returning true without clicking:

```
graphics-fields: FAIL: 7 passed, 6 failed
field-parse:     FAIL: 1 passed, 5 failed
orb-grid:        FAIL: 4 passed, 3 failed
chart-list:      FAIL: 9 passed, 2 failed
```

The sabotage line was then removed by exact string (`grep -c` reads 0) and
the four pass again at 13, 6, 7 and 11. The group names used were checked
to exist in the table first: a misspelt filter prints its own "FAIL: no
test group matches", which would have read like the sabotage biting.

**Suite.** `PASS: 5186 passed, 0 failed`, canary lines identical to item
17's run -- the seven "click every match" loops clicking only the first
match changed no group's outcome.

### Plan item 19 -- one helper for scratch paths, two for pixel counts

**The review's "8 blank-image loops" were two different things.** Read one
by one, the nine loops that compare pixels split into:

- **four that compare against one colour** -- `FPixmapFlatQt()` (any pixel
  unlike the corner), `CpixDrawnQt()` (background, inside a margin), the
  menu-firing group's "chart went blank" (background), and `CpixDifferQt()`
  (the corner);
- **five that compare two images** -- two in `TestScreenOptionsQt()`, two in
  `TestTransitModeQt()`, one in `TestPrintQt()`.

A single `CpixNotBackgroundQt()` would have fitted neither set whole.

**The change.**

- `CpixNotColorQt(pim, rgb, nStep, nMargin)` counts samples unlike a given
  colour. `CpixDrawnQt()`, the menu-firing loop and `CpixDifferQt()` call it,
  **each still passing the colour it compared against before** -- the
  background for the first two, the corner for the third. That the
  menu-firing check and `CpixDifferQt()` have blind spots (N-F, N-G) is
  their own finding; a deduplication that quietly changed what they compare
  would be a second change hiding in the first. `FPixmapFlatQt()` stays
  hand-written: it stops at the first differing pixel and does not count.
- `CpixDiffImagesQt(im1, im2, nStep)` counts differing samples over the
  area both images cover. Checked before relying on that: all four
  full-size loops already require equal sizes before comparing (the two
  in `TestScreenOptionsQt()` skip on a size mismatch, `TestTransitModeQt()`
  checks both), and `TestPrintQt()` already walked the smaller extent. So
  "the common area" is exactly what each walked. Steps kept: 1 at the four,
  2 at the print group.
- `SzScratchPathQt(sz, cch, szName, szSuffix)` builds
  `<temp>/astrolog-qt-<name>-<pid><suffix>`. All 28 scratch-path `sprintf2()`
  calls had that shape; 25 end in `.as`, `.txt` or `.tmp` and are replaced.
  The three whose suffix carries a mode letter or an extension stay. Six of
  the 28 passed a local rather than `QDir::tempPath()` itself; each local was
  traced to `QDir::tempPath().toLocal8Bit()` first, so no file moved.

**Locals the change left unused, removed in the same edit:** `baDir`/`szDir`
in four groups, `x`/`y` in three, and `baTmp`/`szTmp` in the nested include
group -- the last with a comment worth keeping, which moved (below).

**Gotcha: my own unused-variable check counted comment text.** Before
writing, a dry run reported each local's remaining uses in its function.
It said `szTmp` still appeared three times in `TestNestedIncludeQt()`. Two
of those were inside the comment explaining it. The warning audit -- plan
item 3's gate, which this chain runs before building anything -- reported
`unused variable 'szTmp'` and stopped the chain before one test ran. The
gate did its job on the first commit after it existed.

**The comment that went with `szTmp` records a real crash, so it moved
rather than went.** It explained why the temp path is held in a named
`QByteArray`: a `constData()` pointer into a temporary dangles at the
semicolon -- which Linux survived and macOS did not, with a SIGSEGV in
that group. `SzScratchPathQt()` now does exactly that for every caller, so
the explanation, with its evidence, is the helper's comment.

**Falsified: each helper is what its callers now stand on.** Four groups
alone with the helpers in: `chart-render` 125, `screen-options` 21,
`transit-mode` 7, `printing` 7 passed. Then one sabotage at a time, each
reversed by exact string before the next:

```
CpixNotColorQt() returns 0:     chart-render:   FAIL: 102 passed, 23 failed
CpixDiffImagesQt() returns 0:   screen-options: FAIL: 3 passed, 18 failed
                                transit-mode:   FAIL: 3 passed, 4 failed
                                printing:       FAIL: 6 passed, 1 failed
```

The 23 are exactly the 23 graphics charts reading as blank. `grep -c` for the
sabotage marker read 0 afterwards. `SzScratchPathQt()` got no sabotage run:
it builds the same format from the same pieces as the calls it replaced,
and every one of the 25 callers writes its file and reads it back in the
suite, so a wrong path fails them without help.

**Gotcha: a name check that said "missing" for a group that ran.** The
chain first confirms each group name exists in the table, after item 18
showed why. `print` came back with 0 exact matches -- the table row is
`printing` -- and still ran the right group, because `ASTROLOG_QT_TESTS`
matches by substring (`strstr()` in `FTestWantedQt()`), and `printing` is
the only name containing "print". Checked before trusting that run. A
substring filter that happened to catch two groups would have printed one
combined count, which would have read like a result for one.

**Suite.** `PASS: 5186 passed, 0 failed`, canary lines identical to item
18's run.

### Plan item 20 -- structure: result statics, parallel arrays, the fixed 48, one verbose flag

Five review findings in one plan item, one commit. The edit is a script
(`$CLAUDE_JOB_DIR/tmp/item20.py`) with a dry-run mode; every replacement is
confined to its own function and must match its expected count or nothing
is written.

**N1 -- the seven result statics become locals.** Each carried a value out
of a `DriveModalQt()` lambda to the test that asked for it. `DriveModalQt()`
takes a `std::function`, so a capturing lambda does the same job without a
file-scope global. Surveyed first, because three of the seven were read
*outside* the helper that set them:

| Static | Now |
|---|---|
| `s_cTickQt` (timers) | a local captured by reference in both nested lambdas |
| `s_strTimField` | a local in `StrChartInfoTimeQt()`, returned |
| `s_strCombo` | a local in `StrEphemListQt()`, returned |
| `s_strComboWin` | read by `TestEphemerisListQt()`: `StrEphemListQt(QString *pstrWin)` |
| `s_cRowList`, `s_strRow0` | read by `TestChartListFilterQt()`: `FilterChartListQt(int *, QString *)` |
| `s_strLookupQt` | read by one of seven callers: `DriveObjSelQt()` became `StrDriveObjSelQt()`, returning it |

`TestChartListFilterQt()` also reused `s_cRowList` inside an unrelated
lambda of its own; that use got its own captured local. The review's
exceptions stay static, as it said they must: `s_cRangeMsgQt` and
`s_cAtlasRow` are read by C callbacks.

**R2 and N3 -- parallel arrays become one table each.** `TestChartRenderQt()`
(mode and name, 23 rows), `GraphicsChartCaptureQt()` (mode and file, 24) and
`TextChartCaptureQt()` (menu action and file, 8). The script pairs the two
initialiser lists only after checking they have the same length, which is
exactly the hazard R2 named -- nothing compiled checked it before.

**N5 -- the fixed 48.** Three groups snapshotted the chart-mode flags into
`flag rgfSav[48]` and guarded it with `cchartmode <= 48`; two others already
used `QVector<flag>(cchartmode)`. All five use the vector now. The guard in
`TestChartModeTableQt()`'s count check goes; `TestLineDrawingQt()`'s two
`&& j < 48` bounds go; and `TestLongStringsQt()`'s `Check(cchartmode <= 48,
...)` goes entirely, because with a vector it can only pass. **The suite
count moves by one here, on purpose.**

**N12 -- one verbose flag.** Four `getenv("ASTROLOG_QT_TEST_VERBOSE")` calls,
two of them inside per-item loops, read one `static flag s_fVerboseQt` set
once when the table starts, next to the canary's flag.

**F13.** `s_nAnimStartQt` is `static`. Checked first that nothing outside
`qttest.cpp` names it.

**Gotchas in the script, both caught by its dry run before anything was
written:**

- The table-builder took each array's name from the last word of its
  declaration, and for `CONST char *rgszMode[]` that word is `*rgszMode[]`.
  The `*` went into a regular expression and Python refused it. Every
  replacement before that point had already matched its expected count in
  memory; nothing reached the file.
- Adding `static ` pushed `s_nAnimStartQt`'s trailing comment past 79
  columns. The dry run lists new over-long lines; the comment moved above
  the declaration.

**Why this one needs more than the suite.** `GraphicsChartCaptureQt()` and
`TextChartCaptureQt()` run only under `QTGRAPHDIR`/`QTTEXTDIR`, so the suite
never executes them. Their proof is a comparison of the file names each
writes, captured with the binary from *before* the change -- which is why
the verification chain takes its baseline first, before it rebuilds.

**Compared against a baseline taken on the pre-change binary.**

- The six groups whose values now travel through locals, alone, before and
  after: `chart-render` 125, `ephemeris-list` 8, `chart-list` 11,
  `info-time` 5, `timers` 2, `objsel-dialog` 18 -- identical.
- `GraphicsChartCaptureQt()`: 24 PNG names before, the same 24 after.
- Verbose mode: the `[timers: 2 assertions]` line identical with
  `ASTROLOG_QT_TEST_VERBOSE=1`, and no such line without it.

**Falsified: the checks read the new paths, not something left over.**

```
FilterChartListQt() leaves *pcRow unset:
  FAIL  an expression that keeps everything keeps 3 (got -1)
  FAIL  an expression that keeps nothing empties the list (got -1 rows, "")
StrDriveObjSelQt() returns an empty string:
  FAIL  Lookup Names turns that number into a name ("")
```

Both reversed by exact string (`grep -c` for the marker reads 0), and the six
groups back at their baseline counts.

**Gotcha: the text capture comparison proved nothing, and looked like it
passed.** The baseline and the after run both reported "0 text", and the
names "matched" -- 0 against 0. The runner checks `QTGRAPHDIR` first and
*returns*, so with both variables set in one run, `TextChartCaptureQt()` is
never reached. The graphics half is sound; the text half needed its own run.
The pre-change binary was gone by then, but the old file-name list is known
exactly from the source before the edit (radix, wheel, grid, calendar,
influence, ephemeris, aspectlist, midpointlist), so a `QTTEXTDIR`-only run is
checked against that literal list instead.

**The text capture, on its own run** (`QTTEXTDIR` only): "capturing 8 text
charts", no chart reported NOT FOUND, "nothing rendered" or FAILED, and the
files written are exactly the pre-change list -- aspectlist, calendar,
ephemeris, grid, influence, midpointlist, radix, wheel.

**Suite.** `PASS: 5185 passed, 0 failed` -- one fewer than 5186, the
`Check(cchartmode <= 48, ...)` that N5 removed -- and canary lines identical
to item 19's run. **Every count after this item is against 5185.**
