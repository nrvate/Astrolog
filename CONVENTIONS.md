# Conventions

The rules this codebase actually follows, written down so they are
learnable without being burned. Every entry here was verified against
the code (file references inline), not recalled. If an entry and the
code disagree, the code moved: fix the entry.

Line endings: the source is LF, pinned by `.gitattributes` and checked by
`tools/line_endings_audit.py`. Binaries, the files Windows tooling owns,
`font/`, and the `.as`/`.csv` data files the program parses stay as they
ship — `.gitattributes` says why for each (work log item 159). The switch surface has its own document:
REFACTORING.md, "The registry as built".

## Naming dialect

Upstream Hungarian, used consistently enough to trust:

- `f` flag, `n` int, `r` real, `i` index, `c` count, `ch` char,
  `sz` NUL-terminated string, `rg` array, `p` pointer (`pch`, `pcr`),
  `grf` bit set, `pfn` function pointer.
- `*Sav` locals hold a global's saved value around a temporary override;
  the override pattern `fSav = us.fX; us.fX = ...; ...; us.fX = fSav;`
  is everywhere. **New code borrows through the `Borrow` guard instead**
  (astrolog.h): `{ Borrow b(us.fX, newValue); ... }` restores at the
  closing brace on every exit, so a forgotten restore cannot exist and
  the remaining hand-written `*Sav` sites are grep-able. Brace the
  borrow to match sites that restored mid-function; convert old sites
  opportunistically when touched, never as a sweep. One CTAD limit:
  one `Borrow` declaration per deduced type (split
  `Borrow a(x), b(p);` when x and p differ in type).
- Functions: `F...` returns flag, `N...` returns int, `R...` real,
  `Sz...` string, `Ch...` char. `...2` is a variant, not a version.

## Index domains and their "none" values

Two spellings of "none" coexist (the root cause of work log item 38):

- **Object-keyed tables** (`ruler1[]`, `rgObjEso1[]`...): none is `0`.
  `if (ruler2[i])` is a correct test.
- **Sign-keyed tables** (`rules[]`, `rgSignEso1[]`...): none is `-1`.
  `if (rgSignEso2[j])` is a bug; compare `>= 0`.
- `us.objRequire`: `-1` means unset (`FAllow()` in extern.h).

Before testing a table value for "none", check which domain the table
is keyed by. The rulership cross-table invariant is asserted by the
suite's `rulership` group, the esoteric tables' encodings by its
`esoteric-tables` group.

**Documentation typedefs** (astrolog.h, by the domain constants):
`SIGN`, `OBJ`, `HOUSE`, `ASPECT`, `RAY` are all `typedef int` — the
compiler enforces nothing, and that is the point: they exist so a
signature says which domain a parameter indexes. Rule, Borrow-style:
**new and touched signatures use them** (`Dignify(OBJ obj, SIGN sign)`
and the `SzAspect`/`ObjOrbit`/`SetObjDisp` families are the converted
exemplars); existing signatures convert opportunistically, never as a
sweep. A pure conversion must not change object code — the exemplar
commit proved itself with byte-identical checksums of all 31 console
objects. Don't label a parameter that is *not* a domain index:
`ComputeHouses(int)` takes a house *system*, not a house.

**The enforced layer** (T2 step 3, work log items 125 and 128): the
rulership, exaltation and ray tables, and the eight aspect tables, are
checked tables — their subscript takes `SIGT(i)`, `OBJT(i)`, `ASPT(i)`
or `RAYT(i)` (explicit-constructor tags, astrolog.h) and anything else
is a compile error. One domain may hold two extents: the Ray *colour*
tables run 0..cRay+1, where the last slot is an "all Rays" aggregate,
while the Ray *name* tables stop at cRay — each struct asserts its own,
so the aggregate index cannot reach a name table. The tag is a claim:
write it after reading the loop, not mechanically — three sites
legitimately index sign tables by house (the natural-sign
identification) and say so in a comment. Code that selects among
same-domain tables holds a `TBLSIG *`/`TBLOBJ *` (see RULERSYS,
RgRules()); the registry's two `-Y7` rows reach the raw storage as
`.rgn`, the one deliberately unchecked boundary. defaults_audit.py
knows the checked declaration shape (`TBLOBJ name = {`).

**Two tools, and they answer different questions.** A `TBL*` checked
table refuses the wrong *domain* at compile time and says nothing about
range — `rules[SIGT(-1)]` compiles. A `GRD*` range-guarded array is the
opposite: it takes a plain `int`, so converting one changes no call
site at all, and asserts the *range* under the test build. Reach for
`GRD*` when the domain is already obvious and the risk is a bad value —
`-1` used as a subscript is the shape three shipped bugs here have
taken — and for `TBL*` when one domain's table has historically been
indexed with another's. They compose; a table can gain both.

Two aspect
traps, both live: `FIgnoreA()` and `AdjustAspectCount()` apply `ASPT`
inside the macro, so a caller passing a non-aspect is *not* caught —
read the call site; and the `cAspect2`-dimensioned display tables
(`szAspectName` and kin) are a larger domain that stays plain, which
matters because the same variable often indexes both a few lines
apart. Under the
test build (`QTTEST`, which the ASAN build also defines) the subscript
additionally range checks with `AssertIndex` — so a tag carrying an
in-domain but out-of-range value aborts rather than misreading, and
new code should expect the suite to catch that for it.

## Switch-flag macros

- `SwitchF(f)` applies the calling prefix's semantics to a flag:
  plain `-x` toggles, `=x` sets, `_x` clears. The macros read the
  decoded prefix through a local named `pin` (a `PARSEIN *`,
  astrolog.h) — any function using them must take one, which is the
  handler signature's whole contract.
- `ChDashF(f)` picks `'='`/`'_'` when *writing* a flag to a settings
  file, so saved files use forced prefixes and reload identically.
- `inv(v)` is a bare toggle.
- Trap documented at the field declarations (astrolog.h, backend
  selector): a suffixed switch handler may fall through and apply
  `SwitchF` to a second flag — read the handler before assuming a
  suffix is independent.

## Chart-info aliases

`MM DD YY TT SS ZZ OO AA` (extern.h:83-90) alias `ciCore` fields, so
`GetTimeNow(&MM, ...)` mutates a global through what reads as a local.
Upstream-idiomatic; do not rename, but **new code writes `ciCore.mon`
explicitly** rather than extending the alias set.

## Chart position rings (cp0..cp6)

The computed positions live in seven global `CP` slots (extern.h:154),
reached by ring number through `rgpcp[]`, with `rgpci[]` holding each
ring's chart info (`ciCore`, then `ciMain`..`ciHexa`). Ownership,
verified in code (REFACTORING.md C6):

- **`cp0` is the working ring, and `cp0` is "the chart".** Every cast
  lands there — `CastChart()` clears and refills it wholesale — and the
  familiar names `planet`/`planetalt`/`ret`/`chouse`/`inhouse` and
  friends are macro aliases into it (extern.h:111-119). Code that reads
  `planet[i]` is reading `cp0` as of the *last cast, whatever that
  was*.
- **`CastRelation()` (charts2.cpp) is the one systematic writer of
  `cp1..cp6`.** For each ring it loads `*rgpci[i]`, applies that ring's
  `szWheel[i]` switches, casts into `cp0`, and stores `*rgpcp[i] =
  cp0`; it then composes `cp0` from the rings according to `us.nRel`
  (synastry takes chart 1's cusps, composite midpoints them, and so
  on). Single-chart modes never touch `cp1..cp6`, so those slots hold
  whatever the last relationship mode left.
- **The `-r` family handler** (switch.cpp) additionally seeds `cp1`/
  `cp2` from `cp0` around its two `FInputData()` calls, so a
  positions-only chart file (`-o0`, month -1) can serve as either half.
- **The time searches treat `cp1`/`cp2` as scratch.** Transit search
  (charts3.cpp) keeps the natal cast in `cp1` and each time slice's
  cast in `cp2`, restores `cp0` when done — and deliberately not the
  scratch rings. Don't read `cp1`/`cp2` after a search expecting a
  relationship's rings.
- Renderers that reorder rings for display (`XChartWheelRelation` and
  kin) copy through locals or swap `cp1`/`cp2` temporarily; the sphere
  and moons charts save into local `rgcp[]` arrays and restore.

The incident behind this section: relationship charts losing their mode
on recast needed a fork fix and a standing test
(`TestRelationshipModeQt`). If code needs positions to *survive* a later
cast, it must copy the ring aside itself — `cp0` will not.

## Feature macros

`SWISS`, `GRAPH`, `ATLAS`, ... (astrolog.h:82-173) are bare always-on
words. The three that collided with Qt internals were renamed 2026-08-30
(work log item 105): `TIME` is `TIMEFUNC`, `PS` is `PSCRIPT`, `META` is
`METAFILE`, and include order between Qt headers and astrolog.h no
longer matters. Rule: any new feature macro takes a distinctive
compound word like those; never add another bare word.

## Buffers and formatting

- `cchSzDef` = 80 for one output line, `cchSzMax` = 255 for paths and
  long text, `cchSzLine` for reader buffers (astrolog.h:549-551).
- **`sprintf2(S(sz), ...)` is the rule, not the exception.**
  `sprintf2` is `snprintf` unconditionally (this fork made it so after
  finding the unbounded branch live), `S(sz)` expands to
  `(sz), (int)sizeof(sz)`, and `SO(pch, sz)` does the same for a write
  at an offset into `sz` (astrolog.h:413-419). As of work log item 143,
  1,055 sites were swept onto it and the rest threaded a size through by
  hand, leaving **1,141 of the 1,231** formatting calls in this fork's
  own files bounded. Plain `sprintf` in new code needs a reason.
- **Never `S()` a parameter.** `sizeof` is the array's size only where
  the *declaration* is in scope. A destination that arrives as a
  parameter -- `char *sz`, or `char sz[]`, which decays -- has
  `sizeof` 8, so `sprintf2(S(sz), ...)` truncates every string to seven
  characters **and compiles without a warning**. This is the one way the
  bounded idiom is worse than the unbounded one. A function that formats
  into a caller's buffer must take the size as well, **and the convention
  is that the size follows the pointer**, so the call site uses the same
  macro:

      void SzObjSelName(char *sz, int cchMax, int nTyp, int nObj);
      ...
      SzObjSelName(S(sz), od.nTyp, od.nObj);

  `S(sz)` is exactly two arguments. Thirteen functions were converted
  this way (work log items 144-145) and one was deliberately not:
  `WchToUTF8` writes at most four bytes on every branch, so its bound is
  a property of the code, and it says so at the function rather than
  taking a size nobody needs.
- Text charts align by hand-counted spaces. Any change that touches
  layout is verified by tooling, not by eye:
  `tools/chart-matrix.sh` byte-diffs every text chart between two builds
  of *this* tree, and `text-chart-capture.sh` / `text-chart-diff.py`
  compares this port against Windows. The switch matrix does **not**
  cover chart output -- it never renders one.

## Output machinery

The text pipeline is modal global state (REFACTORING.md D3); the modes,
each verified in code:

- `is.S` is where all text goes. `Action()` owns it: opened from
  `-os`'s filename (else stdout) at the top, closed at the bottom.
  `PrintSz()` additionally routes `is.S == stdout` into the chart
  window on the GUI ports (TextCharQt / Win32 TextOut).
  **"Action() owns it" is a hard contract, not a description.** Anything
  in the `PrintSz()` family called from outside an `Action()` — from a
  test, a dialog handler, a hook — writes into a `FILE *` nothing has
  opened. glibc then frees a backup buffer it never allocated, and the
  process dies of heap corruption somewhere else entirely, minutes later.
  That cost an hour of bisecting shared core for a fault that was in the
  caller (work log item 145). To exercise a printing function outside the
  normal flow, set `is.szFileScreen` and go through `Action()` the way
  `TestLongStringsQt()` does, or drive the switch as a separate process.
- `is.nHTML` is the `-kh` HTML context: 0 = off, 1 = content
  (characters entity-escaped, columns counted), 2 = raw markup
  (tags pass through, no column counting), 3 = "no font tag open
  yet" (the state `Action()` leaves after the header, so the first
  `AnsiColor()` skips the closing `</font>`).
- `AnsiColor()` is a tri-state: `us.fAnsiColor` off emits nothing; on
  with `is.nHTML <= 0` emits ANSI escapes; on with HTML emits `<font>`
  tags through the 2->1 dance above. `fAnsiColor >= 2` additionally
  enables reverse video (`kReverse`).
- Column layout is hand-counted spaces. Any change that could touch
  layout is verified with the text-diff tooling
  (`tools/text-chart-capture.sh` / `text-chart-diff.py`), not by eye.
- `PrintS()` (charts0.cpp:177) colorizes the help screens by parsing
  the help text's own characters, with static cross-call state.
- `FieldWord()` accumulates into a static buffer; `FieldWord(NULL)`
  flushes it. Interpretation code depends on that flush discipline.

## Error and return idioms

- Switch handlers return arguments consumed; `tcError` (-1) on error,
  `nSwitchAbsent` (-2) means "not mine", `nSwitchStop` (-3) is
  success-and-stop. `FErrorArgc`/`FErrorValN` print the standard
  messages.
- io.cpp's parsers use single-exit `goto LDone` cleanup. That shape is
  deliberate C resource handling; extend it, don't "fix" it.

## Event loops and adding a command

Four loops own the keystroke/menu surface:

- X11 and the Windows CLI build share `InteractX()` (xscreen.cpp:629).
- The Windows GUI has `WndProc()` (wdriver.cpp), one `WM_COMMAND`
  switch over resource ids.
- The Qt port runs Qt's loop and binds each menu action by **label**:
  `ConnectMenuQt()` (qtdriver.cpp) resolves the label to its command
  id via the generated `qtrccmd.h`, which is also what lets the
  `-~WQ` menu AstroExpression veto/substitute commands.

A new command therefore touches: `astrolog.rc` (menu item + optional
accelerator) and `resource.h` (id); regenerate the three `qtrc*.h`
tables (`tools/rc2qt.py`, `rc_accel.py`, `rc_cmd.py` — CLAUDE.md has
the diff-form); a `WndProc` case for Windows; the Qt handler where the
menu is built; and, if it has a terminal keystroke, `InteractX()`.

**A menu label is an identifier.** The Qt port joins everything by
English label text from `astrolog.rc`; changing a label is an
interface change, and the suite's 42-context-menu/264-shortcut/258-
parity checks are what enforce it.

## Adding a source file

One line in `Makefile.srcs`, in the group it belongs to — core, graphics,
Swiss, or one of the three backends (`SRC_QT`, `SRC_TEST`, `SRC_WIN`).
All five makefiles derive their object lists from it, so nothing else
changes. Before 2026-09-01 this was five edits in five notations and work
log item 96 records the day one was nearly missed. A new `#include` needs
no makefile change either: header dependencies come from the compiler
(`-MMD -MP`).

## Object taxonomy

Chained range constants (`custLo = uranLo`, `oNorm = cobHi`, ...;
astrolog.h:658-683) with composing predicate macros (`FItem`, `FNorm`,
`FThing`, `FCust`, `FStar`...). Insert a category by adjusting the
chain; never compare raw numbers where a predicate exists. For new
per-item tables, the named-row struct pattern (`OBJSET`/`rgobjset[]`,
work log item 63) is the template — flat parallel arrays remain correct
only for what the math sweeps.

## Lifecycle

The contract (what runs once, what is re-entrant, the first-use caches
including the Swiss library's own, ordering constraints) is in
REFACTORING.md under A4. The operational consequence — `-i nrvate.as`
at startup, always — is in CLAUDE.md and QT_TESTING.md with the
verified mechanism.
