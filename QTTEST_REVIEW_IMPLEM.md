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
repaints over leaks.** *(Resolved in the second batch: "N-B and N-D fixed".)* In the baseline, `text-pager` (the group straight
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

**Follow-up, found by the first `make check` of the finished branch: the gate
failed under `make check` and nowhere else.** Every step passed except
`warnings: qttest.cpp`, and its output was not a compiler warning:

```
g++: error: make[1]:: linker input file not found
g++: error: '/nvm/work/qttest': linker input file not found
```

`warning_audit.py --file` learns the build's flags by running a child `make`
and reading its stdout. Under `make check` the parent's `MAKEFLAGS` reach that
child, which then prints `make[1]: Entering directory '/nvm/work/qttest'` to
stdout -- and every word of it became a g++ argument. Reproduced outside make
with `MAKEFLAGS=w MAKELEVEL=1`: exit 2, "qt DOES NOT COMPILE". Without them:
exit 0, empty.

**The falsification above was blind to exactly this.** It ran the gate's
function from a plain shell, which is the one context the gate never runs in.
It proved the gate reads warnings; it could not prove the gate works where it
is installed.

**Fixed at the root**, in `tools/warning_audit.py` rather than in `check.sh`:
`--no-print-directory` on that child `make`, so any caller running under make
gets the flags and nothing else. Re-falsified **under make's environment**:
the current file, exit 0 and empty; `dcd939b`'s file, the gate fails with
exactly the six `-Wunused-variable` warnings and no "DOES NOT COMPILE". Then
`make check` again, below.

**`make check` on the finished branch, second run:** every generated table,
every audit, both builds, `warnings: qttest.cpp ok`, the image and inert-option
audits, both `-W` arity checks, and the suite at `PASS: 5190 passed, 0 failed`
-- "== all clear". (The first run had passed everything but this gate, the
suite included at 5190.)

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
*(All three resolved in the second batch: N-C traced and fixed, N-D with N-B,
N-E fixed -- see those sections.)*

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
*(Resolved in the second batch: N-F fixed, N-G closed with a reason, N-H fixed.)*

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

*(N-I was fixed in the second batch: "N-I fixed: Copy Chart leaves the output file
name alone".)*

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

### Plan item 21 -- the oracle: a dead no-SWISS branch, a check that could not fail, and N-A

**O4 -- the `#ifndef SWISS` branch, measured rather than read.** The review
said the oracle's "built without SWISS" fallback cannot compile, because the
body table above it uses `SE_SUN` and friends unguarded. Two facts decided the
fix. First, **the core still treats SWISS as optional**: about 18 source files
wrap Swiss code in `#ifdef SWISS`, and `astrolog.h` tells a builder to comment
its `#define SWISS` out -- so removing the fallback outright would be wrong.
Second, **this file cannot build without it anyway**. Compiled from a scratch
copy with the define commented out (the define lives in `astrolog.h`, so
`-USWISS` is overridden and the headers had to be copied alongside), it failed
in 15 groups, the oracle being only one. Guarding the oracle table alone would
have fixed nothing. So: the dead branch is gone, and one `#error` near the
includes names the requirement.

**Gotcha: the no-SWISS check's first attempt measured the wrong thing.** Its
flags came from an expansion that dropped the Qt include paths, and the compile
stopped at `QtWidgets/QApplication: No such file or directory` -- which says
nothing about SWISS. The command was taken from `make -n -W qttest.cpp
qt-test` instead, and checked with a control: the same command on the
unmodified file compiles clean.

**Gotcha: my own comment on the `#error` was wrong, and the measurement caught
it.** It said the file would "say so once instead of 72 times". Recompiled
after adding it, the `#error` is indeed the first line of the output -- and GCC
carries on after it, so every undeclared name still follows. The comment now
says exactly that.

**And the error count moved because of this item, which the first guess got
wrong.** Before the change the no-SWISS compile reported 72 errors in 15
functions, the oracle among them with 1. After it, 117 lines match the
same `: error:` pattern: the `#error`, 112 errors in `qttest.cpp` across 15
functions -- the oracle now with 45 -- and 4 more that land in `extern.h`,
from a macro there that names `FObjMidSource`. The working note first blamed two different grep
patterns. It was not that -- both counts used `: error:`. The dead branch had
been doing the one thing it could: the oracle's whole body sat in its `#else`
half, so a build without SWISS never preprocessed it. Removing the branch
exposes that body to the same compile. It changes nothing a real build sees,
since the `#error` now fires first either way, but the number in the comment
is the measured one after the change, not the one from before it.

Measured with the item's final source: the `#error` first, then 112
undeclared-name errors in 15 functions -- the four largest NumericOracle 45, Probe 15, CustomDialogParse 10, AllMenuActions 8.

**O3 -- `cGood`, and the finding it had been hiding.** `cGood = 1` was set
unconditionally at the end of the borrow block and checked after it: a sixth
`Check(fTrue)`. It is now a real check that both restriction sets and five of
the borrowed settings the legs lean on hardest are back where the group found
them. Written that way, it is the net for **N-A** (the oracle leaves
`ignore2[]` changed), which item 3a's canary had found and nothing asserted.
So the fix came in two stages:

- **Stage 1, the check without the fix.** The oracle alone:

  ```
  FAIL  the oracle restored every borrowed setting (14 restriction slots differ)
  FAIL: 575 passed, 1 failed
  ```

  and the canary named the same 14, every one an `ignore2[]` slot. The group
  saved and restored `ignore[]` only; three transit legs write `ignore2[]`.
- **Stage 2, the fix.** Both sets saved and restored, then
  `RedoRestrictions()` (item 3b's lesson). The oracle alone: 576 passed, and
  `[canary: 0 changes left behind by 0 groups]`.

**N-A is closed here.** Its entry under "New findings" above stays as the record
of where it was found.

**O1 -- not done, with a reason.** Splitting the 1,024-line oracle into one
static function per leg was reviewed and left. The legs run inside one
`Borrow` block that pins about 25 settings for all of them, and share locals
(`rgrSwiss`, `xx`, `serr`, `jd`, `iy`); each leg function would have to be
called from inside that block and take or redeclare what it uses. That is a
large, behaviour-neutral diff whose only payoff is structure, in the group
this suite trusts most for correctness. The out-of-order leg numbers in its
comments are recorded, not renumbered.

**Suite.** `PASS: 5185 passed, 0 failed`. Against item 20's canary run the
only difference is one line gone: `[canary oracle: ignore2[2 Moon] 1 -> 0]`,
N-A, which is what the two stages above were for.

### Plan item 22 -- B1: five `Check(fTrue, ...)` that could not fail

**What B1 said.** `TestBadInputQt()` asserted five things with `Check(fTrue,
...)`: that `FileOpen()` returned for a missing file, that
`FProcessCommandLine()` returned after a missing `-i` file, after an unknown
switch and after a 400-digit time, and that `PrintError()` returned rather than
terminating. Reaching each line is the test, as the group's comments say -- but
each counted as a pass, so the suite total carried five assertions that could
not fail, and the pattern was unique to this group.

**The change.** The five calls are gone, their comments kept. A comment at the
top of the group says what actually asserts the behaviour: a call that
terminates or crashes takes the process down before the group's closing
`printf`, and the run ends without that line or the suite's summary. That
closing line now names all four things survived -- it had never mentioned the
400-digit time.

**Not a `Reached()` helper.** The review offered one, printing in verbose mode
and not counting. It would be a sixth kind of statement whose only effect is to
print when nothing went wrong; the closing line already does that, for the whole
group, every run.

**Checked.** No `Check(fTrue` call is left in the file (one textual match, in
the new comment). Warning audit empty. `bad-input` alone: 2 passed, down from 7,
and it prints "survived missing files, a bad switch, PrintError() and a
400-digit time". **The suite count moves by five, on purpose.**

**Suite.** `PASS: 5180 passed, 0 failed` -- five fewer than 5185, the five
removed calls -- and canary lines identical to item 21's run. **Every count
after this item is against 5180.**

### Plan item 25 -- N13: does the atlas sink group leak a city listing into the log? -- closed, no change

(Done before items 23 and 24: it changes only this document, and doing it
between their code commits would have staged it with one of them.)

**What N13 said.** `TestAtlasSinkQt()`'s "console city lookup" leg sets
`pfnAtlasRow = NULL` and calls `DisplayAtlasLookup(..., fFalse, ...)`, which
prints the city row through `PrintSz()` into `is.S`; `TestAtlasZoneQt()`
installs a sink for exactly that reason. "Worth checking the suite log for a
stray city listing."

**The log was checked, and the check could not have seen one.** The full-suite
log from item 20 has nothing after the `== Atlas row sink ==` header, and no
line anywhere shaped like a city row. That would read as "closed" -- but the
question to ask first was whether a listing *could* reach the log. It cannot.
`PrintSz()` (general.cpp) has a Qt branch: when `is.S` is stdout, each
character goes to `TextCharQt()`, into the chart window's text buffer, and the
`} else` after that branch skips the path that writes to the stream. `is.S` is
stdout at that point. So in this build the console leg draws its row into the
chart window, and no log can ever show it, stray or not.

**So N13 is closed with that reason, not with the empty log.** Nothing leaks
into the suite's output. The one row lands in the chart buffer, which the next
render replaces. Installing `SinkAtlasRowQt()` would change nothing observable,
and would stop the leg testing the path it names: the console lookup, with no
sink.

### Plan item 23 -- U1, U2 and `Check()`'s buffer: 255 bytes where a line can be longer

**U1 was less latent than the review thought.** The review called
`CReplaySettingsQt()`'s `fgets(szLine, cchSzMax, file)` latent: "None of the
five filters that use it today select a long switch". One of them does.
`FWantObjDefQt()` replays `-Ye` and `-YD` lines, and a `-YD` line carries an
object name the user typed, with no length limit -- and since item 10 the
settings-strings sweep writes 300-character names on purpose.

**U2 is theoretical, and checked directly anyway.** `FEqSzPrefixQt()` ends a
switch at `szLine[i] <= ' '`; with `char` signed, a byte above 0x7F is negative
and passes that test. No settings writer puts a non-ASCII byte straight after a
switch name, so no replay can hit it -- which is why the net is two direct
calls to the helper, not a round trip.

**Both nets written first and seen failing, then the fixes.** A two-stage
script (`$CLAUDE_JOB_DIR/tmp/item23.py`):

- **U1 net**, inside the Object Selections dialog group's existing save and
  replay: name the slot with a 300-character name, save, clobber, replay
  through `CReplaySettingsQt(szPath, FWantObjDefQt)`, and require the whole name
  back. The leg then puts "Chiron" back, because the six cases after it start
  from what case 0 left.
- **U2 nets**, before the interface-settings replay: `"-WF\xC3\xA9 x"` must not
  match `-WF`, and `"-WF x"` must -- the control, so a helper that matched
  nothing would fail too.

**Stage 1, the nets without the fixes**, each group alone:

```
objsel-dialog:      FAIL  and replays whole, not cut at 255 (233 characters back)
                    FAIL: 19 passed, 1 failed
interface-settings: FAIL  a switch followed by a UTF-8 byte is not that switch
                    FAIL: 42 passed, 1 failed
```

Exactly the new check in each, and nothing else.

**Why 233 and not something near 255.** The name did not lose its tail at the
255th byte of the *name*. For a customised slot the writer (`FOutputSettings()`,
the custom-object loop) puts the definition and the name on **one line** --
`-Yeb 34 2060 -YD 34 "LongSlotName..."` -- so about 21 characters of switches
come before the name starts. `fgets()` keeps 254 bytes of the line, and 254 - 21
is 233. The rest of the line was read as the next "line" and run as a command
line of its own, which is the second half of what U1 described.

**Stage 2, the fixes.** `CReplaySettingsQt()` reads into a `cchSzLine` buffer
with `fgets(szLine, cchSzLine, file)`; `FEqSzPrefixQt()` compares
`(uchar)szLine[i] <= ' '`. The same two groups alone: `objsel-dialog` 20 passed,
`interface-settings` 43 passed -- two more each than before the item (18 and
41), because each net is two checks.

**`Check()`'s own buffer, shown with a sabotage because no passing run can.**
`Check()` formats its message into a local buffer, and only a *failing* check
prints it, so its size is invisible to a green suite. A temporary
`Check(fFalse, ...)` with a 400-character message went into the `timers` group
for both builds:

- stage 1, buffer `cchSzMax`: the failure line printed at **262** characters --
  cut short (`vsnprintf()` truncates safely; the message is just lost);
- stage 2, buffer `cchSzLine`: printed at **424** characters, whole.

The temporary check was then removed (`grep -c` for its marker reads 0), and
the warning audit is empty.

**Suite.** `PASS: 5184 passed, 0 failed`, canary lines identical to item 22's
run. **Four** more than 5180, not the three first predicted: the U1 leg is two
checks (the long name is written; it replays whole) and the U2 net is two (the
negative case and its control). The prediction miscounted; the per-group
counts above (18 to 20, 41 to 43) agree with the measured total. **Every count
after this item is against 5184.**

### Plan item 24 -- H4, L1, N7: a safety net that could not see popups, and file opens nobody checked

**H4 -- the dialog net.** `StrOpenDialogQt()` arms two timers: one to capture
and close the dialog it opened, and a net 1.5 seconds later in case that close
did not take. The first looked for `activeModalWidget()` and then fell back to
`activePopupWidget()`; the net looked only for the modal one. A dialog that came
up as a popup and survived its first close would have hung the run. The net has
the same fallback now.

**Not falsified, and said so.** Proving H4 would mean making a dialog open as a
popup *and* ignore its first close, which none of the 25 does and nothing in the
suite can arrange without changing the dialogs themselves. The change is two
lines, mirroring the ones directly above it in the same function, and `dialogs`
still passes at its count. That is the evidence there is.

**L1 -- the recursion group's files.** `TestFileRecursionQt()` writes two chains
of settings files, one comfortably inside the include-depth limit and one past
it, then asserts the first loads and the second is refused. Every `QFile::open()`
was `if (open) { write }` with no else. On a full or read-only temp directory the
second chain would be missing and "refused" would pass -- for having nothing to
include. One check per chain now counts the links that could not be written
(one check per file would add dozens of lines), and the file that includes
itself gets its own check.

**N7 -- two groups `fprintf()` to a `FILE *` nobody checked.**
`TestNestedIncludeQt()` (two files) and `TestGraphicsModeSourceQt()` (one) called
`fopen()` and wrote straight through the result. A full temp directory would be
a segfault, not a FAIL. Each open is now checked and the write guarded.

**Baseline, before the change**, each group alone: `file-recursion` 5,
`nested-include` 5, `graphics-mode` 5, `dialogs` 131.

**After the change**, each group alone: `file-recursion` 8 (+3),
`nested-include` 7 (+2), `graphics-mode` 6 (+1), `dialogs` 131 (unchanged).

**Falsified: the opens fail, and the new checks say so.** Each sabotage reversed
by exact string before the next.

- **L1**, every chain file and the self-including file opened read-only (so
  every open fails):

  ```
  FAIL  all 15 files of the chain were written (15 not)
  FAIL  a chain 15 deep loads, and its last file is read
  FAIL  all 25 files of the chain were written (25 not)
  FAIL  the file that includes itself was written
  FAIL: 4 passed, 4 failed
  ```

  The line that is *not* there is the finding. "A chain 25 deep is refused
  before the stack goes" still **passed** with no files written -- a chain that
  does not exist is refused very reliably. That is exactly what L1 predicted,
  and before this item nothing in the group would have said so. Now the check
  directly before it does.
- **N7**, the three scratch files opened for reading instead of writing (so
  every `fopen()` returns NULL): `nested-include` failed 6, the two new checks
  among them; `graphics-mode` failed 2, the new check among them. **Neither
  process crashed** -- both exited with the ordinary failure status, and the
  groups went on to fail their own checks on the missing files. What the same
  sabotage did *without* the guards was not run; that it would crash in
  `fprintf(NULL, ...)` is the review's claim, and glibc's documented behaviour,
  not a measurement made here.

No sabotage line left afterwards, and the diff was back to the intended 47
lines.

**Suite.** `PASS: 5190 passed, 0 failed` -- six more than 5184, the six new
checks -- and canary lines identical to item 23's run.

## Outcome

Every item of the phased plan is accounted for below: a commit, closed with the
evidence that killed it, or not done with the reason. Branch `qttest`, in the
linked worktree `/nvm/work/qttest`, one commit per item, nothing merged.

| Plan item | Finding(s) | Outcome | Commit |
|---|---|---|---|
| 1 | W1 (debug line) | done by `dcd939b` before this work | -- |
| 2 | T1, T2, T3 | fixed; T2's field list corrected by measurement | cff7b1a |
| 3 | warning gate | added to `make check` | e7001ff |
| 3a | canary | added, and extended through items 7, 8 and 15 | 5f431c3 |
| 3b | K8 | fixed | d074eb3 |
| 4 | K1 | fixed | 6a82aa5 |
| 5 | K2 | leak did not reproduce; robustness change made | fc5af7d |
| 6 | K3 | fixed | aeb6183 |
| 7 | K4, K5 | fixed | e39fbc6 |
| 8 | V1 | fixed | 627a57e |
| 9 | O2, O5, O6 | fixed | 653464d |
| 10 | W1 (markers) | fixed | 3941283 |
| 11 | R1 | fixed; three text-only charts left the render list | 4eb0cde |
| 12 | M4 | **not worked in the first batch**; worked in the second, see "M4 worked" | -- |
| 13 | S3 | closed with evidence, no change | 4a710c6 |
| 14 | S1 | fixed | a3c5d51 |
| 15 | S2, K6 | fixed, plus the same fault in `chart-export` | c54e976 |
| 16 | K7 | fixed | 9e4ba14 |
| 17 | comment pass, H2, D1, E3 | done, comments only | c717d3f |
| 18 | C1-C4, G1 | done | f9444d6 |
| 19 | scratch paths, pixel loops | done | e8d60fd |
| 20 | N1, R2, N3, N5, N12, F13 | done | 99d1e15 |
| 21 | O3, O4 (and N-A) | done; O1 not done | 5571562 |
| 22 | B1 | done | 0fafc22 |
| 23 | U1, U2, Check buffer | fixed | e0cfadf |
| 24 | H4, L1, N7 | fixed; H4 not falsified | 9d699c0 |
| 25 | N13 | closed with a reason, no change | 0c7d615 |

**Plan item 12 (M4) was not worked, and this is the one gap in the table.**
(Decided and worked in the second batch below: `menu-actions` restores everything.) It
asked to check that the menu-firing group's restore covers everything a macro's
settings file can reach. The canary's first run answered part of it -- after
`menu-actions` 130 restriction slots, 66 scalars and both star lists are
changed -- but deciding which of those the group *should* restore, as opposed to
leave for the groups after it to pin, is a judgement about that group's design
that no item here made. Left for the maintainer, with that measurement.

### The suite's count, and every place it moved

| After | Count | Why |
|---|---|---|
| baseline (`dcd939b`) | 5198 | |
| item 11 | 5186 | three text-only charts left the render list, four checks each |
| item 20 | 5185 | the `cchartmode <= 48` guard check, which only guarded the 48 |
| item 22 | 5180 | five `Check(fTrue, ...)` |
| item 23 | 5184 | two new checks each for U1 and U2 |
| item 24 | 5190 | six new checks: one per recursion chain, the self-including file, three scratch-file opens |

Every other item left the count where it was, and every full run above was 0
failed.

### Findings this work turned up

- **N-A** (the oracle left `ignore2[]` changed) -- **fixed** in item 21, with a
  real check that failed first.
- **N-B** `text-pager`, **N-D** four more groups -- clear chart-type flags
  through `SetChartModeQt()` and do not put them back. **Fixed** (second batch).
- **N-C** `gs.yWin` 600 to 575 after groups that open certain dialogs. **Traced
  and fixed** (second batch: the window was sized to the chart, not around it).
- **N-E** `dialog-buttons` leaves `us.nAsp` changed. **Fixed** (second batch).
- **N-F** the menu-firing "chart went blank" check has no margin, so the border
  alone passes it. **Fixed** (second batch).
- **N-G** `CpixDifferQt()` compares against the corner. **Closed with a reason**
  (second batch).
- **N-H** `GraphicsChartCaptureQt()` captures `gExo`, a text-only chart. **Fixed**
  (second batch).
- **N-I** (port, not the suite) -- vector Copy Chart leaves a deleted temp file's
  name in `is.szFileOut`, where Windows sets `gi.szFileOut` only. **Fixed**
  (second batch).

### Not resolved, in one place

*(Status as of the third batch, 2026-09-13. Each item below was open when this
list was written.)*

- The warning gate in `make check` covers `qttest.cpp` only (item 3). **Worked in
  the third batch** -- see "The warning gate covers every Linux build".
- A macro name containing a double quote does not survive `forced-positions`'
  restore (item 4). **Fixed** (second batch, "Tidy-ups"), and the settings
  writer's own quoting fixed in the third batch.
- No standing net for K3 or K4: both are falsified with hand-built command lines
  that no ordinary run makes (items 6, 7). **Worked in the third batch** -- see
  "Standing nets for K3 and K4".
- Dialog buttons are found by label, not by `IDOK`/`IDCANCEL` (item 18).
  **Fixed** (second batch).
- The oracle is still one 1,024-line function (O1, item 21). **Closed by the
  maintainer's decision** (2026-09-13): not split. A long function with no defect
  to its name is not a finding to act on.
- `DriveModalQt()` still needs its forward declaration (D1's structural half,
  item 17). **Fixed** (second batch).
- T4 and T5 (a macro and a double initialisation in `TestSortStarQt()`) were P3s
  with no plan item. **Fixed** (second batch).
- H4's fix is not falsified (item 24). **Worked in the third batch** -- see
  "H4 falsified".

### Gotchas worth carrying forward

- **An exact-string edit that fails does not stop a shell chain.** Item 15's
  first attempt: the replace refused, and a build, a solo run and a suite ran on
  the old binary and looked like a verification. Read an edit's own output and
  the binary's timestamp before anything after it.
- **Check what a measurement cannot see before believing it.** Several times in
  this work a clean result was an instrument that could not have failed: the
  corner-pixel blank check (item 11), a text capture that never ran because the
  runner returns after the graphics capture (item 20), an unused-variable count
  that matched comment text (item 19), an atlas log that the Qt `PrintSz()`
  never writes to (item 25), and a no-SWISS compile stopped by a missing Qt
  header (item 21).
- **The canary's own blind spots**: `is`/`gi`/`ci*` beyond the fields named,
  `rgobjset[]` and the colours; clock-driven `gs.rRot`/`gs.rTilt` differ run to
  run; reals print with `%g`; stderr can split a line in a redirected log.
- **Prove a gate in the context it runs in.** Item 3's warning gate was
  falsified from a plain shell and failed under `make check`, because make's
  environment changed what a child `make` prints. The first `make check` of the
  finished branch found it; nothing else could have.
- **Sabotages here were always reversed by exact string**, never by
  `git checkout`, and every one was confirmed gone with `grep -c` before the
  run that followed.

---

# Second batch: the deferrals, decided and worked

After the squash of `qttest` into `qt` (`b0e6fa5`), every deferral above was
gone over with the maintainer. Branch `qttest2`, in the linked worktree
`/nvm/work/qttest2`, from `qt` at `b0e6fa5`; one commit per item again, and a
squash merge on approval. Baseline on that commit: `PASS: 5190 passed, 0
failed`, canary 201 changes in 10 groups.

## Decisions

| Deferral | Decision |
|---|---|
| N-I, Copy Chart leaves a temp name in `is.szFileOut` | fix to match Windows: the copy path does not touch `is.szFileOut` |
| M4 (plan item 12), `menu-actions` leftovers | `menu-actions` restores what it changes; no general per-group reset |
| N-B, N-D, N-E, N-F, N-G, N-H | fix each one the evidence makes certain |
| N-C, `gs.yWin` 600 to 575 | trace the 25 pixels, then fix (below) |
| warning audit speed | build an incremental, cached audit |
| buttons by `IDOK`/`IDCANCEL` | do it; name About's OK button `IDOK` |
| six groups assigning a stack path to `is.szFileOut` | convert to item 15's save by content |
| D1, T4, T5, a double quote in a restored macro name | do them |
| O1, splitting the oracle | not in this batch |

**A general state reset was considered and not taken.** The core has no reset to
compiled defaults: `us`, `is`, `gs` and `gi` get their values once, from the
initialisers in `data.cpp` and `xdata.cpp`; `InitProgram()` resets only pointer
tables, restrictions, colours and the custom-object tables, and
`InitVariables()` only the volatile flags the `-Q` loop needs. A reset would have
meant snapshotting the whole state at table start and restoring it before every
group. The maintainer chose `menu-actions` restoring itself instead.

## N-C traced: the 25 pixels are the menu bar, and the chart shrinks on every save

A temporary probe (`ProbeQt()`, removed afterwards) printed the window, the
scroll-area viewport, the canvas, `gi.qim` and `gs.xWin`/`gs.yWin` around opening
and cancelling the Restrictions dialog:

- the menu bar is **25 pixels** high, **29** under `nrvate.as`'s larger menu font --
  exactly the two drops the canary had recorded (600 to 575, 1558 to 1529);
- `gs.yWin` was **already** the smaller number when the probe started, before any
  dialog. The dialog groups were only the first to force a graphics repaint after
  startup; `paintEvent()` then adopts the canvas height ("Window Resizes Chart").

**Cause.** Startup sizes the *whole window*, menu bar included, to the saved chart
size: `gi.qwind->resize(gs.xWin, gs.yWin)` in `qtdriver.cpp`, just before
`show()`. The chart area comes out a menu bar shorter, and the first repaint writes
that back into `gs.yWin`. **Square Screen** does the same with the same call.
The port already has the right routine, `ResizeWindowToChartQt()`, which keeps
whatever of the window is not viewport; startup and Square Screen do not use it.

**Windows does it the other way round.** `gs.xWin`/`gs.yWin` are the *client*
area there (`WM_SIZE` stores `wi.xClient`/`wi.yClient`), and `WinMain` calls
`ResizeWindowToChart()` before `ShowWindow()` -- which grows the window so the
client area is the chart size, and does nothing in text mode.

**It compounds.** Three real launches, each loading the settings file the previous
one saved, starting from `:Xw 760 600`:

| Launch | Loaded | Window | Chart area | Saved |
|---|---|---|---|---|
| 1 | 760x600 | 760x600 | 760x575 | 760x575 |
| 2 | 760x575 | 735x575 | 735x550 | 735x550 |
| 3 | 735x550 | 710x550 | 710x525 | 710x525 |

The width follows from the second launch on because the saved file carries `=XQ`
(square charts): squaring a wheel from the shortened canvas takes the width with
it. With `_XQ` added to the same file, the width does not shrink that way.

So a user with a graphics chart who saves settings loses 25 pixels of chart height
every launch. **Decision: fix to match Windows**, startup and Square Screen.

## N-C fixed: the window is sized around the chart

**The net first.** New group `startup-chart-size` (`TestChartWindowSizeQt()`; first registered as `window-size`, a name already taken -- see "N-C follow-up"), two legs:

- **Startup**, reachable only from a fresh process. `run-qt-tests.sh` gains a
  "Chart size at startup" section that launches the group alone with
  `:Xw 760 600 =X` and `ASTROLOG_QT_WINSIZE_PROBE=760x600`; the group then
  requires the chart viewport **and** `gs.xWin`/`gs.yWin` (what a save writes) to
  be 760 by 600 after the first paint. The runner accepts only
  `PASS: [1-9]... passed, 0 failed`, checked against the real pre-fix failure line,
  an empty filter match and a two-digit failure count.
- **Square Screen**, in the full suite: from a 700 by 500 chart, trigger the item
  and require the viewport and `gs` to equal what `SquareX()` makes of 700 by 500
  -- not "width equals height", since `SquareX()` adds the sidebar and leaves maps
  and grids alone.

Before the fix, both failed exactly as traced: startup "760 by 575, wanted 760 by
600; window 760 by 600", and Square Screen "viewport 740 by 471 ... wanted 740 by
500" under `nrvate.as` (the 29-pixel menu bar).

**The fix** (`qtdriver.cpp`): Square Screen calls `ResizeWindowToChartQt()` where it
called `resize(gs.xWin, gs.yWin)`, in Windows' order (resize, then `us.fGraphics`).
`BeginQt()` keeps its plain resize -- a text chart still wants it, and
`ResizeWindowToChartQt()` returns in text mode as Windows' does -- and calls
`ResizeWindowToChartQt()` **after** `show()`.

**Gotcha: not before `show()`.** The first attempt measured before showing, laying
the window out first with `layout()->activate()`. It measured the chrome as 122 by
25 instead of 0 by 25 and opened an 882 by 697 chart: a hidden `QScrollArea` has
had no resize event, so its viewport is not laid out whatever the main window's
layout says. After `show()` the pending resize events have been delivered and the
measurement is exact. Nothing adopts the interim size, because `paintEvent()` only
writes the viewport back once `InteractQt()` has set `qi.fReady`.

**Not fixed here, found on the way: under `nrvate.as` the width grows 80 a launch.**
Under `nrvate.as` the same probe opened 840 by 600 for `:Xw 760 600`.
Traced with temporary prints: `gs.xWin` is **already 840 on entry to `BeginQt()`**.
The shared core's `#ifdef ISG` branch in `FActionX()` (`xscreen.cpp`,
`else if (fSidebar) gs.xWin += SIDESIZE...` just before `BeginX()`) adds the
sidebar, the canvas adopts it, and a save records 840 -- so the width grows 80 a
launch, which is the "920 wide" the drift experiment saw with `_XQ`. It is not the
menu bar, not a window minimum (69 by 98) and not this fix. It is **not the sidebar
switch alone**: `-Yi1 ephem :Xw 760 600 =X =Xv0` comes up 760 wide, so some other
setting in `nrvate.as` decides whether `fSidebar` holds on that path (the macro
reads `gi.nMode`). Windows' own `ResizeWindowToChart()` adds the sidebar too,
whenever `gi.nMode == 0`, so what Windows does over a launch and a save has to be
**measured** before the port is changed; that is the next item.

**Gotcha in the probe itself.** A first version added `_Xv0` to keep the sidebar
out of it. `=X _Xv0` ends the process at once, exit 0, **no output at all** -- the
runner read the empty string as the assertion failing -- and `_Xv0 =X` lets the
tree's square-charts default make 760 by 600 into 600 by 600. The probe is plain
`:Xw 760 600 =X`, the command measured failing before the fix and passing after.
Why `=X _Xv0` exits silently is its own question, recorded to look at.

**Measured on Windows afterwards, and it changes the question.** The real
`astrolog.exe` under Wine on a private display, `-Wt -Yi1 ephem :Xw 760 600 ... =X`,
window geometry read with `xdotool` after a ten-second settle, each case run twice
in swapped order with identical results:

| Switches before `=X` | Window |
|---|---|
| `_Xv0 _XQ` | 936x635 |
| `=Xv0 _XQ` | 936x635 |
| `=Xv0` | 936x635 |
| (`=X _Xv0`, graphics turned off) | 1432x873 |

The width **does not depend on the sidebar switch at all** on Windows. Its startup
`ResizeWindowToChart()` adds `SIDESIZE * gi.nScaleText / 2` whenever
`gi.nMode == 0`, which it is before the first chart is drawn, and `WM_SIZE` then
stores the client width in `gs.xWin`. So a Windows launch followed by a save
records a width larger than it loaded as well. This fork's settings writer saves
`:Xw` **verbatim** -- upstream subtracted the sidebar on save and `NSwXw()` added it
back, and `io.cpp` records why that pair was removed -- so the growth is in both
builds, in the file format, and in a fix this fork made on purpose. **For the
maintainer:** not changed here. The Qt port is left matching its oracle, as the
decision for N-C asked. *(Superseded the same day: fixed in both builds -- see
"The chart width grew by the sidebar at every launch -- fixed, both builds".)*

**`=X _Xv0` turns graphics off, on both builds.** `NProcessSwitchTable()`
(`switch.cpp`) calls `SwitchF2(us.fGraphics)` for every `grfSwGraphics` flag row, so
the prefix of `_Xv0` is also the prefix of graphics mode. Windows then opens its
text window (the 1432x873 above). The Qt binary exiting 0 **with no output at all**
is the same fact on this build, and by design: the `graphics-mode` group
(`TestGraphicsModeSourceQt()`) asserts that `_X` on a command line still selects
text mode, because that is how `run-qt-tests.sh` runs this build to completion with
no window -- the window is created inside `FActionX()`, which `Action()` reaches
only with graphics on, so the suite, which runs from `InteractQt()`, never starts.
(First recorded here as untraced; traced the same day.) The probe does not say
`_Xv0`.

## N-I fixed: Copy Chart leaves the output file name alone

**The net first.** New group `copy-chart-name` (`TestCopyChartNameQt()`): put a
marker name in `is.szFileOut`, trigger **Copy Chart SVG** from the menu bar, then

- require the SVG source on the clipboard, so "the name is unchanged" cannot pass
  on a copy that returned early (under `-0o`, say);
- require `is.szFileOut` to still be the marker.

It restores `is.szFileOut` by content and `us.fGraphics` afterwards. Before the fix:
`FAIL is.szFileOut still names what it named before the copy (now
"/tmp/astrolog-qt-copy-CXzEMF")`, the other three checks passing -- so the copy
ran and the name was overwritten, exactly as N-I said.

**The fix** (`qtdialog.cpp`): `FExportChartQt()` gains `flag fFileOut`, and only
clones into `is.szFileOut` when it is set. `gi.szFileOut` is still set every time,
since `FActionX()`'s file writers read that one. Callers:

| Caller | `fFileOut` | Windows |
|---|---|---|
| `ShowExportGraphicsDialogQt()` (the five Export items) | true | `wdialog.cpp` clones the chosen name into both after `GetSaveFileName()` |
| `FExportChartToFileTestQt()` (the suite's picker-less Export) | true | it stands in for the Export items |
| `CopyChartVectorQt()` (Metafile, PostScript, SVG, Wireframe) | false | `wdriver.cpp`'s copy case clones into `gi.szFileOut` only |

Copy Chart Bitmap and Text never went through this: the first copies `gi.qim`, the
second captures the text chart.

After the fix `copy-chart-name` passes (4) and so does `chart-export` (22), which
drives the same function through the Export hook.

**The full suite:** `PASS: 5195 passed, 0 failed`, startup section ok. Against the
N-C run the canary loses exactly one line -- `menu-actions: is.szFileOut "(NULL)"
-> "/tmp/..."` -- and nothing else moves.

## M4 worked: `menu-actions` puts back everything it changes

**Decided:** no general per-group state reset (see "Why no general state reset"),
and `menu-actions` restores what it changes itself.

**Measured first.** Snapshot before the sweep, compare after, no restore: **89**
fields and arrays differed. The canary had named the scalars, the restriction slots
and the star lists; the comparison added ten whole arrays it cannot see --
`ignore`, `ignore2`, `ignorea`, `rgobjset`, `rAspOrb`, `force`, `ruler1`, and the
derived `rules`, `rules2`, `kObjA`. So M4's own worry -- per-object state a macro's
settings file reaches, orbs and colours -- was real. Object display names, custom
star names and macros did **not** change; they are restored anyway so the snapshot
is the whole configuration rather than the part that moved today.

**The vocabulary is the three sweeps', not a hand list.** `MENUSTATEQT` holds:

- every member of US and GS, through `settingsfields.h` (the `settings-fields`
  sweep's table), scalars by value and strings by content;
- the 24 arrays of `settings-arrays`, whose table moved from inside
  `TestSettingsArraysQt()` to file scope with two accessors, `CSetArrayQt()` and
  `PvSetArrayQt()`, since `menu-actions` is defined 8,000 lines earlier;
- object display names, custom star names and macros, as `settings-strings` holds
  them;
- "Window Resizes Chart" and "Chart Resizes Window", which live in `qi` and no
  sweep sees.

**The restore** follows the idioms those groups proved: window flags first (one of
them resizes as it is set), arrays by byte copy, scalars by value, strings through
`FCloneSz()` only where they changed, the star pair through `FProcessYXU()`, object
names through `SetObjDisp()`; then the caches computed from them
(`is.fSwissPathSet = fFalse`, `InitColorPalette()`), `RedoRestrictions()` and
`AdjustAspectCount()` as `settings-arrays` ends, `ResizeWindowToChartQt()`,
`RedoMenuQt()`, `RecastAndRedrawQt()`. The check runs after all of that, so a
restored value that a recompute then overwrote is caught too.

**The net:** `the sweep put back every setting it changed (%d still differ)`,
naming the first eight. Falsified twice: with no restore at all, **191** differ
(counting restriction slots one by one, as that version did); with the array
copy-back skipped, 15 differ and it names `ignore`, `ignore2`, `ignorea` and the
category flags and `us.nAsp` derived from them.

**Two groups were depending on the leftovers,** and both **failed when run alone**
before any of this -- they had passed only in the full suite:

- `menu-resync` restricted the Uranians in `ignore[]` only and expected
  `SyncRestrictMenuQt()` to clear `us.fUranian`. `RedoRestrictions()` counts a
  category as included while *either* set leaves a body unrestricted, so it only
  passed while `menu-actions` had left `ignore2[]` restricting them. It pins both
  sets now. Its alone-failure before the fix is its falsification.
- `screen-colors` asserted "a colour wheel is mostly not grey" counting the
  background, and black is grey: 99% on `nrvate.as`, where it only passed because
  the sweep had left thick lines, a filled decan ring and a 100% background image.
  It counts grey among drawn pixels now. Falsified by forcing that pass to
  monochrome: 100% grey, and it fails.

Both pass alone under `-i nrvate.as` and `-Yi1 ephem`.

**The full suite:** `PASS: 5196 passed, 0 failed`, startup section ok. The canary
goes from **199 changes left behind by 9 groups to 10 by 7** (four of the ten are
`gs.rRot`/`gs.rTilt`). N-E's `us.nAsp` line is gone with the leftovers that caused
it. What remains is three chart-flag leaks (`chart-render` `us.fListing`,
`bad-input` and `shared-core` `us.fHorizon`, which is N-B/N-D) and `gs.yWin`
moving by a few pixels in the three groups that change the interface font --
visible now that "Window Resizes Chart" is no longer left off.

## N-B and N-D fixed: every group puts the chart-type flags back, not just the mode

**The fault.** `SetChartModeQt(nModeSav)` is not a restore: it clears every flag
in `rgchartmode[]` and sets the one whose row maps to `nModeSav`. A group that
found no chart-type flag set -- which is how a settings file leaves them -- left
one set.

**Why the chase stopped working.** After M4 the canary named three groups, each
reproduced alone: `chart-render` `us.fListing 0 -> 1`, `shared-core` the same,
`bad-input` `us.fHorizon 0 -> 1`. Fixing them unmasked `ray-digit-fill`; fixing
that unmasked `aspect-dash`; then `star-sort-outputs`. Each was real, and each had
been invisible only because a group before it already left the flag at 1, so its
own leak changed nothing the canary could see. A canary that compares each group
with the one before cannot see a leak into state that is already leaked.

**So the measurement changed: every group run alone.** A script ran all 107 groups
one per process under the canary, four at a time -- about 28 seconds -- and listed
what each left behind **starting from a fresh launch**. That is the inventory the
chase could never produce, and it is how the rest were found at once.

**The fix.** `CHARTFLAGSQT`, `SnapChartFlagsQt()`, `RestoreChartFlagsQt()` and
`RestoreChartModeQt(nMode, &saved)`, which puts the mode back and then the flags
(every `rgchartmode[]` flag plus `us.fAtlasLook` and `us.fZoneChange`, the two
`SetChartModeQt()` clears outside that table).

**The flags go back last, with nothing drawn after them.** The first version
redrew after restoring, and the inventory still showed `chart-store` and
`expression-hooks` leaking. A **text** chart drawn with no chart-type flag set picks
the listing itself -- `charts1.cpp`, `us.fListing = fTrue; // didn't indicate
anything` -- so a redraw after the restore, in a group that ended in text mode,
re-created the leak. `lockdown` and `text-export` leak through that same line
without ever calling `SetChartModeQt()`, so they take `RestoreChartFlagsQt()`
alone.

**Where it is used.** 22 restores in 23 functions' worth of groups:

- the first three: `chart-render` (twice -- its second half fires the Chart menu
  and ends in a restore of its own), `shared-core`, `bad-input`;
- the unmasked: `ray-digit-fill`, `aspect-dash` (which never put the mode back at
  all), `star-sort-outputs` (its `-S` switch changes the mode);
- every other bare `SetChartModeQt(nModeSav)` or `(nSav)` in the file, converted
  by pattern with each snapshot inspected in place: `rising-gradient`,
  `menu-side-effects`, `text-pager`, `chart-scroll`, `screen-colors`, `chart-now`,
  `text-extent`, `screen-options`, `jet-trail`, `credit-colors`, `transit-mode`,
  `chart-store`, `expression-hooks` and `GraphicsChartCaptureQt()`;
- flags only: `lockdown`, `text-export`.

`expression-hooks` needed its snapshot moved: the sweep put it beside the mode it
saves, halfway down the function, after a text redraw had already set the listing,
so it faithfully restored the leaked state. It is taken at the top now.

`bad-input` has no chart-mode call; its deliberately bad switch `-ZZzzz` is read as
far as `-Z`, the horizon chart, and a chart-type switch is routed through
`SetChartModeQt()`.

**Evidence.** Before: the canary lines above, each with its group alone. After: the
alone inventory over all 107 groups names **no chart-type flag in any group**; the
only lines left are the two interface-font groups' `gs.yWin` (N-J). No per-group
assertion was added: checking the flags straight after restoring them cannot fail;
the inventory is the measurement.

## N-E fixed: the Aspect leg of `dialog-buttons` puts `us.nAsp` back

`ClickInModalQt()` clicks the button **and then OK**, and Aspect Settings' OK
derives `us.nAsp` from `ignorea[]`. The leg saved and restored `ignorea[]` only.
From this machine's settings the toggle never changes the highest unrestricted
aspect, so nothing showed -- after M4 the canary line was gone. It showed while
`menu-actions` left aspects restricted.

**Reproduced before fixing**, with a temporary precondition (aspects 6 onward
restricted, as those leftovers had them) and a temporary switch to skip the fix,
one build, removed afterwards:

```
nAsp at leg start 5, after restore 0     (no fix -- N-E's own "5 -> 0")
nAsp at leg start 5, after restore 5     (fix)
```

The fix saves `us.nAsp` with the flags and puts it back with them.

## N-F fixed: the menu-firing "chart went blank" check can see a blank chart

It counted samples over the whole image, where the border and the header and
footer lines alone are 800-900 -- "more than 20" passed a chart that drew nothing.
It now uses `CpixDrawnQt(16)`, the in-margin measure `chart-render` got in item 11,
with the same threshold.

**Measured first:** over all 341 menu items on `nrvate.as`, the lowest in-margin
count is **93**, after "Solar System &Orbit" (then 667 Local Horizon, 1649 Moons).
**Falsified:** filling the buffer with the background just before the count made
**322** items fail "chart went blank" -- every item that reaches the check.
Reverted by exact string and rebuilt before anything else ran.

## N-G closed with a reason: `clear-screen` is not blind with the corner

`CpixDifferQt()` compares against `pixel(0,0)`, and R1's worry was a border
standing in for the background. `clear-screen` never draws, though: it paints the
whole buffer one colour, checks that is uniform **and** not the background, clears,
then checks the buffer is uniform against the corner **and** that the corner is
`gi.kiOff`. The last two together say every sampled pixel is the background, which
is the claim. No change.

## N-H fixed: the graphics capture no longer writes a blank Exo chart

`GraphicsChartCaptureQt()` (`QTGRAPHDIR`) still listed `gExo`, which `chart-render`
dropped in item 11 because `DrawChartX()` has no case for it. Before: 24 captures,
and `exo.png` at 1798x1558 held **two colours**, 2,793,370 background samples and
7,914 border. After: 23 captures and no `exo.png`. The comment now names all three
text-only charts.

## N-C follow-up: the new group's name was already taken

The chart-size group added for N-C was registered as `window-size`, and an older
group, `TestWindowSizeQt()`, has had that name all along. Nothing noticed:

- `ASTROLOG_QT_TESTS` matches by substring, so the "Chart size at startup" probe in
  `run-qt-tests.sh` ran **both** groups -- the older one first, in table order,
  which is exactly what a probe of *startup* state must not do;
- the per-group canary inventory kept one summary file per name and counted 106
  groups of 107, which is how it surfaced.

**Renamed** `startup-chart-size`, which no other group name contains and which
contains none; the probe now reports `1 of 107 groups matched`, and passes.

**And the class is closed:** the table runner asserts every group name appears
once, as it already asserts `ok-settles` is last. Falsified twice -- against the
tree before the rename (`group name "window-size" is in the table twice`), and in
its final single-assertion form against a temporary second `credit-colors` entry
(`1 repeated; first "credit-colors", entries 20 and 21`), reverted by exact string.
The first form made one `Check()` per pair, 5,671 passes on every run; it counts
quietly and asserts once now.

**The full suite after all of the second batch's suite items (N-B/N-D, N-E, N-F,
N-G, N-H and this):** `PASS: 5197 passed, 0 failed` -- one more than M4's 5196,
the duplicate-name check -- and the startup section passes. The canary is down to
**7 changes left behind by 5 groups**: `gs.rRot`/`gs.rTilt`, and the three
`gs.yWin` lines of N-J (`dialog-buttons` moves it back by one after
`console-font`). No chart-type flag appears in any group. The seven commits for
these items were built from one working tree by hunk: each commit's `qttest.cpp`
was generated from HEAD with that commit's hunks, every removed line checked
against HEAD, the last equal to the tested tree byte for byte, and each
intermediate compiled on its own (with the final one compiled as the control for
that check).

## New finding, open: N-J -- changing the interface font moves `gs.yWin`

The alone inventory leaves exactly two lines, both in groups that change the
interface (menu) font and put it back: `console-font` `gs.yWin 1558 -> 1557` and
`interface-settings` `gs.yWin 1558 -> 1562`. The menu bar's height follows the
font, the window keeps its size, and with "Window Resizes Chart" on the canvas
writes the new viewport height back into `gs.yWin`. That part is how Windows
behaves too, when its own menu font changes.

What is not explained is that restoring the font does not restore the height: the
chart comes back 1 and 4 pixels different, not equal. Unmeasured: whether the menu
bar returns to its first height after the reapply, and whether the canvas adopts
before or after it. A user who changes the interface font and changes it back
would save a chart a few pixels off. **Open, for the maintainer**; not fixed
without measuring. *(Superseded the same day: measured and fixed -- see "N-J fixed:
an interface font change keeps the chart size".)*

## The warning audit, incremental: `tools/warning_audit.py --cached`

**Decided:** make the audit fast by caching, and require its report to match the full
audit's exactly.

**Why a cache has to keep what the compiler said.** The full audit is five clean builds
(seven with Qt6) because a compiler reports nothing about a file it does not compile:
an up-to-date object is silence, not a clean bill. So the cache keeps each object's
compiler output beside it and the report is read from those, not from a build log.

**How.**

- **Objects only, never linked, outside the tree** (`WARNING_AUDIT_CACHE`, default
  `~/.cache/astrolog-warning-audit/<build>-<key>`; per checkout since the gate work
  below found a shared cache could hide an edit). No binary in the tree is touched,
  `make clean` cannot reach it, and a session building in the tree is not disturbed.
- **A second makefile replaces the compile rule.** `make -f Makefile.qt -f rule.mk`: a
  pattern rule with the same target and prerequisites as the real one replaces it, and
  its recipe is the real recipe with the output kept in `<object>.warn`. Every makefile
  here compiles with a literal `g++` or `$(CXX)` that no command-line variable wraps,
  so the rule is the only place to stand.
- **A key over what make does not track:** the flags as make expands them (so a new
  `$(QT_CFLAGS)` counts), the makefile and `Makefile.srcs` (the Swiss `-Wno-*`
  exemption is there), the compiler's `--version`, and the replacement rule itself. A
  changed key is a fresh directory; old keys for that build are removed.
- **Header edits** need nothing extra once `-MMD -MP` go back into the flags -- the
  audit's own flags replace the makefiles', which carry them -- because make's own
  dependency files then decide what recompiles.
- **Guards.** An object with no `.warn` stops the run: if the replacement rule ever
  stopped replacing, the real rule would build silently and the report would read
  clean. And `--cached --update` is refused: the baseline is written from a full clean
  build, which is what the cached mode is measured against.

**Measured.** On this machine, all five builds plus both Qt6 legs: a cold `--cached` run (empty
cache) **1 min 39 s**, the same as the full audit's 1 min 39 s; a second run with
nothing changed **0.48 s**; after an edit to `astrolog.h`, which every file includes,
1 min 42 s, because every object really does recompile; after an edit to one `.cpp`,
only that object in each build.

**Proven against the full audit.** Two warnings planted at once -- an unused variable in
`io.cpp` and an unused `static` in `astrolog.h` -- then `--cached` and the full audit
run on the same tree. Both reported exactly the same four NEW lines, compared
sorted and byte for byte, including the header warning's per-build counts (29, 31 and
32 files), which is the part a per-object cache could most plausibly get wrong:

```
NEW  console+qt+qt-test+wcli+win  io.cpp      FOutputSettings  -Wunused-variable   1
NEW  console+wcli                 astrolog.h  (header)         -Wunused-variable  29
NEW  qt-test                      astrolog.h  (header)         -Wunused-variable  32
NEW  qt+win                       astrolog.h  (header)         -Wunused-variable  31
```

Then both plants reverted by exact string (no marker left): the next `--cached` run
recompiled and came back clean -- no stale `.warn` survived -- and the one after took
0.47 s. The rule text went into the key before this proof, so the proof ran against
the final key.

## Buttons by IDOK and IDCANCEL

**What changed.** `PpbButtonQt()` and `FClickButtonQt()` find a dialog's button by
**object name**, `IDOK` or `IDCANCEL`, instead of by its label. All 37 calls pass
the name, and the four drivers that compared labels inline -- the About group,
`ClickInModalQt()`, `objsel-lookup` and `timers` -- use the helpers.

**Why names.** A label is display text: a mnemonic moves in it, and a theme or a
translation can rewrite it. Windows never finds these buttons by what they say;
`IDOK` and `IDCANCEL` are the ids `astrolog.rc` gives them in all 25 resource
dialogs, and `qtdialog.cpp`'s builder makes every control's id its object name.

**The one dialog outside the resource.** About builds a `QDialogButtonBox` by hand,
so its OK had no name. It gets `setObjectName("IDOK")`.

**And a quiet fallback made loud.** `ClickOkInModalQt()` -- which `ok-settles`
uses to press OK in every dialog, twice -- closed a dialog it found no OK in and
moved on. Closing is a cancel, so a dialog it could not press OK in passed
"pressing OK settles" by never being asked. It now fails by the dialog's title,
and adds no passes when it succeeds.

**The net, and it failed first.** With the suite switched to names and About not
yet named: `ok-settles` alone failed three times with `"About Astrolog" has no IDOK button to press` (57 passed, 3 failed) -- the dialog the old label lookup had been pressing by its text, and the one the old fallback would have cancelled without a word. Then About's button named: `ok-settles` alone passed 57 of 57, and so did the groups that drive buttons, each alone: `about-version` 4, `objsel-lookup` 15, `timers` 3, `dialog-buttons` 11, `graphics-fields` 14, `objsel-dialog` 21, `objsel-glyph` 6. The full suite: 5197 passed, 0 failed, the same count, since the new check adds no passes; the canary unchanged.

## Tidy-ups: D1, T4 and T5, and the macro-name quote

**D1.** `DriveModalQt()` is defined where it was forward-declared, above the About
group that first uses it, and the declaration and its apology are gone. It uses
`QTimer`, `QApplication` and `nScaleTest` (defined at the top of the file), so
nothing it needs moves.

**T4.** `SORTSTAR_LEG` was a five-statement macro that assigned `fileT` behind the
caller's back. It is a lambda now, `fileT = FileLeg("...")`, so what it assigns is
at the call, and the command is an argument to `"%s"` rather than itself the
format string.

**T5.** `fFortune` was initialised at its declaration and reset to the same value
just before its only use. The reset is gone; the declaration keeps the initialiser,
which is the file's convention.

**The macro-name quote.** Three restores put a saved macro name back as
`-WM 1 "%s"`. A name holding `"` put the rest of itself on the command line as
switches. `SzMacroNameSwitchQt()` quotes with whichever of `"` and `'` the name does
not contain -- the rule `FOutputSettings()` already applies to AstroExpressions,
and `NParseCommandLine()` honours both -- and says so, rather than corrupting, for
a name holding both. (The settings writer's own `-WM` line always uses `"`; that is
recorded here, not changed. *Superseded: the writer was fixed in the third batch,
for every string setting, not only `-WM` -- see "The settings writer quotes every
string so it reads back".*)

**Evidence.** The quote fault needs the `"` to be followed by a space or punctuation --
the command-line parser only ends a quoted word there -- so a first probe named
`Probe"Quote` came back intact before the change too, and proved nothing. Started with
macro 1 named `Probe" x`, `interface-settings` and `forced-positions` each left
`macro name 1 "Probe" x" -> "Probe"` before the change (which also shows the startup
rename took), and neither leaves a macro-name line after it (44 and 10 passed).
`star-sort-outputs` passes alone after T4/T5 (7). Each of the three commits was built
on its own. Full suite on all three: `PASS: 5197 passed, 0 failed`, no canary change
outside `gs.rRot`, `gs.rTilt` and N-J's `gs.yWin`.

## The pointer-assigned output names, saved by content

**What was left from item 15.** Item 15 converted the groups that *cloned* into the
user's output-name buffer and "checked and left alone" the ones that only point
`is.szFileOut` at a buffer of their own by plain assignment and put the pointer
back -- latent, since a stack address sitting in `is` is freed or written through
by anything in the window that clones into or frees that field (`FExportChartQt()`
for Export, `-o` on a command line, `InitVariables()`, `FinalizeProgram()`). The
deferral named "six groups"; re-derived against the current tree it is **nine
sites**:

`FSaveSettingsToQt()` (the helper `ok-settles` saves through), `settings-roundtrip`,
`interface-settings`, `objsel-dialog`, `settings-fields`, `settings-arrays`,
`settings-strings`, `forced-positions` and `lockdown` (whose buffer was a
`QByteArray`'s, the same hazard). `chart-export` keeps item 15's deliberate hybrid.

**The change.** Each saves `is.szFileOut` as a `QByteArray` plus whether it was set,
puts its own path in through `FCloneSz()`, and restores with `FCloneSz()` of the
saved text or of NULL. No address of a group's own buffer reaches `is` at all.
A script made the nine edits, each scoped to its own function with every match
asserted exactly once; afterwards the only `is.szFileOut =` assignment left in
the file is `chart-export`'s.

**What can and cannot be shown.** No assertion can fail before this change: the
hazard needs something in a group's window that frees or clones the field, and
none of the nine reaches one today. What can be shown is that nothing moved.

Each of the nine groups was run alone under the canary, started with `-i nrvate.as -o`
and a 144-character path, so any clone into that buffer lands in place. Before and after
the change every group matched once, left no canary line, and reported the same count:
`settings-roundtrip` 15, `interface-settings` 44, `objsel-dialog` 21,
`settings-fields` 4, `settings-arrays` 4, `settings-strings` 4, `forced-positions` 10,
`lockdown` 13, `ok-settles` 57. **The measurement can see the name:** `lockdown`'s
restore sabotaged to `FCloneSz(NULL, ...)` made the canary name the 144-character path
going to NULL; reverted by exact string and rebuilt. Full suite:
`PASS: 5197 passed, 0 failed`, canary unchanged.

## The chart width grew by the sidebar at every launch -- fixed, both builds

**First recorded as open (N-C follow-up) and left for the maintainer. It should not
have been; it is fixed.**

**Cause, in both builds, one root.** Upstream saved `:Xw` with the sidebar taken off
and added it back at startup. This fork made `:Xw` verbatim (for a documented reason:
the subtraction depended on the chart mode, which is not saved) and removed the
subtraction, but two startup additions survived:

- **Windows**, `ResizeWindowToChart()` (`xscreen.cpp`): the window was sized to
  `gs.xWin` **plus the sidebar whenever `gi.nMode == 0`**, which is exactly startup;
  `WM_SIZE` then stored the wider client width in `gs.xWin`, and a save wrote it.
- **Qt**, `FActionX()`'s `ISG` branch: `else if (fSidebar) gs.xWin += sidebar` before
  `BeginX()`; the canvas adopted the wider viewport and a save wrote it. This add was
  also quietly compensating for a second fault: the squaring branch took the sidebar
  off before squaring and put it back **only under `#ifdef WIN`**, so under Qt a
  square wheel with its sidebar was squared down to its height and then re-widened by
  that add.

**Fix.** `ResizeWindowToChart()` no longer adds a sidebar (Windows and WCLI); the
`ISG` add is `#ifndef QT` (X11 keeps it -- there `gs.xWin` excludes the sidebar and
nothing adopts the window size back); and the squaring branch handles the sidebar under
Qt as it does under Windows.

**Evidence.**

- Qt, new second probe in `run-qt-tests.sh`'s "Chart size at startup": `:Xw 760 600
  =Xt =Xv0 _XQ =X` came up **920 by 600** before (760 + the 160 sidebar at the
  default text scale) and 760 by 600 after. Fixing only the `ISG` add made the
  original probe (`:Xw 760 600 =X`, square with sidebar) come up 600 wide -- which is
  how the squaring half was found; with both halves both probes pass.
- Windows, the real `astrolog.exe` under Wine, same switches, window geometry before
  and after the change built from this tree: **936 -> 776** wide, with the sidebar on
  and off alike -- down by exactly the 160 the startup term added, so the client area
  now equals the saved width.
- Full suite: `PASS: 5197 passed, 0 failed`; size groups alone pass (`window-size`,
  `startup-chart-size`, `graphics-size`, `screen-options`, `chart-render`,
  `menu-side-effects`).
- Visible consequence on `nrvate.as` (`:Xw 1600 1558`, square charts, sidebar): the
  window now comes up with the square chart plus sidebar -- `gs.yWin` 1360 -- where it
  was the requested height with the chart squared inside it. That is the Windows
  squaring rule, now applied to Qt at startup.

## N-J fixed: an interface font change keeps the chart size

**First recorded as open. It should not have been; it is fixed.**

**Cause, measured** (offscreen, window 1798x1587). The menu bar's height follows the
interface font -- 29 px for `nrvate.as`'s Fira Code Retina 14, 30 for JetBrains Mono 13,
15 for Liberation Sans 6, 42 for Liberation Sans 24 -- and `ApplyUiFontQt()` left the
window size alone, so the chart viewport absorbed the difference and, with "Window
Resizes Chart" on, `ChartCanvas::paintEvent()` wrote it into `gs.xWin`/`gs.yWin`, where
a save records it. The restore itself was not asymmetric (the saved face gives 29 px
again); the bar only takes its new height on the next event-loop turn, so a group that
ended with a re-apply still pending left a stale height (`console-font`
`gs.yWin 1558 -> 1557`), and `interface-settings` restored the menu font *setting*
without applying it at all, leaving Liberation Serif 12 and a 25 px bar
(`gs.yWin 1558 -> 1562`).

**Fix.** At the end of `ApplyUiFontQt()`, with "Window Resizes Chart" on, the main
window is laid out at once (`layout()->activate()`) and `ResizeWindowToChartQt()` fits
the window around `gs.xWin` by `gs.yWin` -- the window resizes, the chart does not.
With the flag off the window is the user's to size and nothing is chased. Windows has no
interface font, so parity is "the chart size does not move".

**The net.** New group `ui-font-chart-size`: interface font to 24 pt and 6 pt and back,
chart size and viewport checked after each. On the unfixed code it failed 2 of 6
(`a 24 point interface font keeps the chart 1798 by 1558 (chart 1798 by 1545 ...
menu bar 42)`, and 1572 at 6 pt); with the fix 6 of 6, the window becoming 1600 and
1573 while the viewport holds. **`activate()` is needed**, falsified: removed, the group
fails the same two checks again (1360 -> 1347 and 1374 on the current tree); restored,
it passes. Each group alone leaves no canary line now.

**And the font `interface-settings` left behind.** Its restore now calls
`ApplyUiFontQt()` after putting the settings back. Measured with temporary prints:
the application font was Fira Code Retina 14 at the group's start and **Liberation Serif
12** at its end before the call, Fira Code Retina 14 after.

# Third batch: every open item, resolved

## The settings writer quotes every string so it reads back

**Found** while checking the second batch's own "recorded, not changed" note about the
`-WM` line, then measured wider. Every string setting was written between bare double
quotes -- search paths (`-Yi`), the atlas and colour files (`-Y5i`, `-YkE`, `-YkU`),
the exoplanet and star lists (`-YUx`, `-YRU`, `-YXU`), object display names (`-YD`),
the sidebar text (`-YXt`), the default chart name and location (`-zj`), every macro
(`-M0`), the menu names (`-WM`, `-WM0`, both builds) and the two font names (`-WF`,
`-WG`); `-YU`'s star definition had no quotes at all. `NParseCommandLine()` ends a
quoted parameter at its quote character followed by a space or punctuation, so any of
those holding `"` followed by a space -- a macro such as `-i "my file.as"` is exactly
that -- ended early, and a refused line stops `FProcessSwitchFile()` reading the rest of
the file. The chart-file writers (`-zi`) were never affected: `PrintQuotedSz()` turns a
`"` into `'` there.

**The net came from the sweeps, not a hand-picked probe.** The `settings-fields` and
`settings-strings` markers now carry `" q-`, and `interface-settings`' menu-name probe
`Probe" MacroName`. On the unfixed writer: `settings-fields` "the file it wrote loads
back" failed and every field written after the first broken line read as lost (40+
names, from the category flags to the AstroExpression hooks); `settings-strings` lost
`-YD`, `-YU` and `-M0`; `interface-settings` lost the macro names and everything the
file carried after them.

**The fix** is one helper, `PrintQuotedParamSz()` (`io.cpp`), at all of those sites:
quoted with `"`, or with `'` when the text holds a `"` -- the rule the AstroExpression
writer already used. Text holding both cannot be quoted exactly; its double quotes are
written as single ones, as chart files always have, so the line loads and the lines
after it are not lost. `settings-strings` gives that fallback its own marker (macro 1
holds both characters, expected back with `'`).

**Evidence.** After the fix the three groups pass alone: `settings-fields` 335 of 342
asked, 0 lost; `settings-strings` 280 asked, 0 lost; `interface-settings` 44 passed.
The fallback falsified: writing both-quotes text raw inside `"` made
`settings-strings` fail "the file it wrote loads back" and lose `is.rgszMacro[1]`;
reverted by exact string, 0 lost again. `tools/warning_audit.py --file io.cpp` compiles
it under all five builds, Windows and wcli included, with no warning.

## H4 falsified

**H4, falsified after the fact.** No real dialog is a popup that ignores its first
close, so the suite now has one. `ShowIgnoringPopupQt()` (`qttest.cpp`) shows a
`Qt::Popup` widget and blocks in its own `QEventLoop` as `exec()` would; its
`closeEvent` refuses the first close and accepts the second. It also carries its own
5 s limit and records when that limit is what ended it, so a broken net is a FAIL, not
a hung run. The `popup-net` group drives it through `StrOpenDialogQt()` and requires:
the opening timer finds it by title, the net ends it rather than the limit, exactly two
closes, return within 4 s, and no popup left open.

**With the net's `activePopupWidget()` fallback disabled** (a marked sabotage), the group
exits 1 at 5009 ms with three FAILs -- the limit ended it, one close only, and the
elapsed time; without the opener's own limit that run would never have ended.
**Restored** (exact-string revert, no marker left), it passes 7 of 7 in 1559 ms, the
canary is clean, and the full suite passes 5206, 0 failed. It costs about 1.5 s, the
net's own delay. (`loop` is a macro in `astrolog.h`, which is why the event loop member
is named `evl`.)

## Standing nets for K3 and K4

Both fixes had been falsified only on hand-built command lines, because no ordinary
run carries a long star list or a chart list. Each now has an assertion that runs every
time.

**K3.** `star-links` gives its own save a **1799-character** name list and a
**1379-character** link list, and asserts both come back byte-identical, before putting
the user's lists back from an outer copy. With the old `cchSzLine` buffers filled by
`sprintf2()` put back (a marked sabotage) it fails `a 1799 and a 1379 character star
list come back whole (1019, 1019)`; reverted, 10 passed.

**K4.** `ChartListSeedQt` appends ten distinct charts before the pin in
`export-roundtrip`, `null-names`, `chart-list`, `open-dir` and `oracle`, asserts every
entry by date, time, longitude and name after the group's `Restore()`, and trims them
off -- so each group sees only its own seed whatever runs before it. With a count-only
restore (the K4 shape) all five fail, e.g. `open-dir puts back the charts it found in
the list, not just their count (10 of 10, first wrong entry 0: "Default")`; reverted,
20 / 5 / 13 / 7 / 578 passed, canary clean. Six passes added in all.

**A correction to plan item 7.** The oracle legs' pre-fix "save only the first eight
entries" shape loses nothing even with ten charts seeded: each leg empties the list and
appends at most four, so the entries past the eighth are never overwritten. What the net
catches in `oracle` is a count-only restore, which the other four share.

## The warning gate covers every Linux build

**The warnings gate in `make check`.** `tools/check.sh` runs
`tools/warning_audit.py --cached --gate-subset --build console --build qt --build
qt-test`, which compiles every source file and header of the three Linux builds with the
audit's `-Wall` flags. Before 2026-09-13 the step compiled `qttest.cpp` alone, and every
other file reached the compiler only through the full audit, which nothing ran. A subset
run is normally not a gate -- its first column renames shared sites -- so
`--gate-subset` makes it one on "any warning at all", which holds only while
`tools/warnings.txt` is empty; it refuses (exit 2) once it is not. win, wcli and Qt6 stay
with the full audit, because a release runner has no mingw or hand-installed Qt6.
`WARNING_AUDIT_JOBS=1` narrows the build on a shared machine.

**Falsified under `make check` itself**, which is where the earlier gate had broken: an
unused variable planted in `io.cpp` failed the step (`console+qt+qt-test io.cpp
FileOpen -Wunused-variable`, rc 2), one planted in `astrolog.h` failed it across all
three builds, and with both reverted by exact string the step reads `warning gate clean:
0 warnings across console, qt, qt-test`. Warm 0.26 s, cold about 40 s; a header edit
recompiles what includes it.

**And a bug in the cached audit, found while building this.** The cache was one
directory shared by every checkout on the machine. make compares file times inside its
own tree, so objects written from one worktree could be newer than another worktree's
edit and hide it -- a new warning unreported. The cache is now per checkout,
`~/.cache/astrolog-warning-audit/<checkout hash>/`. The "Measured" and "Proven" results
in the cached-audit section above were taken with one checkout using the cache, so they
stand; what they could not have seen is two.

## Merging qt: `info-coord` assumed seconds were shown

`qt` moved by four commits while this batch was open, and merged into `qttest3` with no
text conflict. `make check` on the merged tree then failed two checks in `info-coord`,
the chart info coordinate group `qt`'s newest commit added: `and it still reads back as
the longitude set ("97:44W")` and the latitude's. The fields carry seconds only when
`us.fSeconds` is on; `make check` runs the suite with `-Yi1 ephem`, which reads the
tree's `astrolog.as`, and that has `_b0`. So the group failed under `make check` on `qt`
itself -- it had passed only in runs with `-i nrvate.as` (`=b0`). Not a merge fault
and not a leak: the canary run named no group changing `us.fSeconds`.

**Fix:** the group pins `us.fSeconds` on and restores it. **Net:** alone, as `make
check` runs it, the unfixed binary fails the two read-backs and the fixed one passes 8
of 8, also with `_b0` forced; `make check` on the merged tree: all clear, 5218 passed,
0 failed.

# Fourth batch: a clang gate, before the v8.00-qt.20 release

## Why

`v8.00-qt.19`'s first release run failed on macOS ("No clang warnings") and on Windows
(MSVC compile) for two arrays in `qttest.cpp` sized by a pointer difference,
`byte rgbChartSav[(pbyte)&us.fVelocity - (pbyte)&us.fListing]`. g++ folds that to a
constant in silence, Apple clang warns (`-Wgnu-folding-constant`), MSVC refuses it
(C2131). `ab451d7` fixed the code; nothing local could have caught it. The maintainer
asked for a current clang on this box and a gate before `.20`.

## `tools/clang-gate.sh`, in `make check`

Both Qt builds' objects (`Makefile.qt`, `Makefile.qt.test`) compiled with the newest
`clang++` on the machine, with the makefiles' own `CPPFLAGS` -- the macOS job's build --
and the log held to `tools/ci-assert-clang-clean.sh <log> 0`, that job's assertion. The
mechanism is the cached warning audit's: a second makefile replaces the compile rule and
keeps each object's output, the cache is per checkout, and the key covers the compiler
version, the warning flag, the makefiles and the rule. An object without its output
stops the run. No clang is a skip.

**The first draft was blind, and the proof said so.** With the `.19` code planted back
into `qttest.cpp`, clang 22 reported the tree clean. Measured on a small probe holding
the same two declarations: clang 22 and clang 14 give **0 warnings** with default flags
and even with `-Wgnu-folding-constant` asked for. Newer clang no longer folds such an
array; it treats it as a real variable-length array and says so only under
`-Wvla-cxx-extension` (in `-Wall`); clang 14 under `-Wvla-extension`. The Apple clang of
the macOS runner (LLVM 17 era) still folds and warns by default. So the gate names that
one class explicitly, choosing whichever spelling the compiler accepts. It does not take
all of `-Wall`: the gate stays the release lanes' rule plus the one class both of them
fail on.

**Proven.** Plant restored: clang 22 fails `qttest.cpp:8589:26: warning: variable length
arrays in C++ are a Clang extension [-Wvla-cxx-extension]`; clang 14 fails the same line
under `-Wvla-extension`. Reverted by exact string: clean, 0 warnings in our sources and 0
in vendored Swiss. Timings at `-j2`: 70 s cold, 0.3 s with nothing changed.

**Setting up clang here, recorded because both steps failed once.** Linux Mint 21.2 is
Ubuntu jammy underneath and its archive stops at clang 14; `apt.llvm.org`'s
`llvm-toolchain-jammy-22` repository supplies clang 22.1.8 (LLVM's `llvm.sh` does not
recognise Mint, and the repository's directory listing is served gzip-compressed, which
defeated a first attempt to read the version from it). And any clang on this box failed
on `<type_traits>` until `libstdc++-12-dev` was installed: clang takes its C++ library
headers from the newest GCC install it finds, GCC 12, whose headers were missing.
