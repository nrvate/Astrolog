# Conventions

The rules this codebase actually follows, written down so they are
learnable without being burned. Every entry here was verified against
the code (file references inline), not recalled. If an entry and the
code disagree, the code moved: fix the entry.

Line endings are covered by their own hard rule in `CLAUDE.md` (upstream
files CRLF, fork files LF, CR count == line count per file). The switch
surface has its own document: REFACTORING.md, "The registry as built".

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
suite's `rulership` group.

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
- `sprintf2` is `snprintf` unconditionally (this fork made it so after
  finding the unbounded branch live); bounded formatting is the rule
  for any new sprintf into a shared buffer.
- Text charts align by hand-counted spaces. Any change that touches
  layout is verified by the text-diff tooling (`text-chart-capture.sh`
  / `text-chart-diff.py`), not by eye.

## Output machinery

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
