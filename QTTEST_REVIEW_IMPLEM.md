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
