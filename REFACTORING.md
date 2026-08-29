# The standing refactoring review

This is the catalog of what makes Astrolog hard to evolve and what to do
about it. The goal is a codebase someone could read without cringing:
maintainable, flexible, modular — while every increment keeps the program
byte-for-byte behaving as it does today, under the nets this project
already trusts (the 3027-assertion suite, ASan, the settings round trip,
the four rc audits, and the Windows build as oracle).

It is a **living document worked across many sessions**. The survey
ledger below says which parts of the codebase have been reviewed and
which haven't; a session picks the next pending area, reads it deeply,
adds findings, and updates the ledger. Findings accumulate here; when one
is acted on, its entry records the commit and moves to Done.

## Ground rules

1. **Evidence or it doesn't go in.** Every finding cites file:line, and
   wherever possible the *incident* — the actual bug this design already
   caused, usually recorded in QT_GUI_PLAN.md's work log. A finding with
   a crime-scene photo outranks any amount of taste.
2. **Behavior-preserving steps only.** No increment may change what any
   build computes, prints, or saves. `tools/settings-round-trip.sh`,
   `./run-qt-tests.sh`, ASan, and where relevant a text-chart diff
   against the Windows build are the proof, run before every commit.
3. **Windows parity remains the spec** for the Qt port. Refactoring the
   shared core must keep `Makefile.win` compiling and behaving
   identically — it is the behavioural oracle precisely because it
   doesn't move.
4. **The upstream-merge bridge is already burned** (work log item 63, by
   the maintainer's explicit decision), so "upstream won't take this" is
   not a veto. It is still a cost: prefer shapes upstream *could* take.
5. **A refactor needs a net before it needs a plan.** Where a subsystem
   has no test coverage, the first increment is the test that pins
   current behavior — written so it fails when the behavior breaks
   (reintroduce the bug and watch it fail; two tests in this project were
   caught asserting an invention).

## Non-goals

- **No big-bang rewrite**, no new language, no framework.
- **Don't fight the Hungarian dialect.** `rgobjset`, `fNot`, `cchSzMax`
  are consistent and greppable; renaming thousands of identifiers buys
  churn, not clarity. Document the conventions instead (see T8).
- **Third-party code is out of scope**: `swe*.cpp/h` (Swiss Ephemeris),
  `placalc*.cpp`, `swemptab.h`. Only the *boundary* our code presents to
  them is reviewable.
- **Don't "fix" a Windows quirk in the shared core** without checking the
  real Windows build first; several look like bugs and are load-bearing
  (`inv()` on non-boolean fields, gotcha 6 in QT_GUI_PLAN.md).

## How a session works this document

1. Open the survey ledger, pick the topmost area still `pending`.
2. Read the area's files properly — structure, data flow, who calls what
   — not just grep. Note anything that matches an existing theme (tag it
   with the theme ID) and anything new.
3. Write findings in the format below. Update the ledger row with the
   date and a one-line verdict.
4. If a finding is small, safe, and fully netted, it may be *done in the
   same session* — record the commit hash in the finding.
5. Commit the doc update itself.

**Finding format:** `ID` — title. *Evidence* (file:line, metrics).
*Incident* (work-log item or session where this design drew blood; "none
yet" is allowed but weakens priority). *Direction* (the shape of the
fix, not necessarily the plan). *Cost/risk*. Priority is the section it
sits in: themes and area findings are ordered most-worth-doing first.

## Survey ledger

| Area | Files (lines) | Status |
|---|---|---|
| Metrics pass, whole codebase | all own `.cpp/.h` | **done 2026-08-29** — numbers cited throughout |
| A. Spine: startup, switch parsing, dispatch | astrolog.cpp (3538) | **surveyed 2026-08-29** — findings A1-A5 |
| B. Settings & serialization | io.cpp (3211) | **surveyed 2026-08-29** — findings B1-B5 |
| C. Computation core | calc.cpp (4000), matrix.cpp (703), ephemeris glue | **surveyed 2026-08-29** — findings C1-C6 |
| D. Text charts & interpretation | charts0-3.cpp (8876), intrpret.cpp (1605) | **surveyed 2026-08-29** — findings D1-D4 |
| E. Graphics core & device layer | xscreen.cpp, xgeneral.cpp, xdevice.cpp, xdata.cpp (8971) | **surveyed 2026-08-29** — findings E1-E3 |
| F. Graphics charts | xcharts0-2.cpp (9129) | pending |
| G. Frontends & satellites | qtdriver/qtdialog (9345), express.cpp (2936), atlas.cpp (2171) | pending |
| H. Data model & headers | astrolog.h (2457), extern.h (1243), data.cpp (1702) | pending |

The area order is deliberate: B and C sit under everything else, and
what the themes prescribe (a settings table, index types) lands there
first. wdriver.cpp/wdialog.cpp are surveyed only as the oracle — they
are upstream's and stay upstream-shaped.

---

## Cross-cutting themes

These are the designs that recur in every file. Ordered by how much
blood each has actually drawn.

### T1 — One program-wide mutable state, and everyone edits it

*Evidence:* Four global structs — `US` (~252 fields, astrolog.h:1720),
`GS` (~87), `GI` (~85), `IS` (~67) — plus **636 `extern` declarations**
in extern.h. On the order of 8,000 `us.`/`gs.`/`gi.`/`is.` references
across the codebase. Because every function reads and writes the same
state, callers that need a temporary change do a manual save/restore
dance: the `*Sav` idiom appears ~60 times in xcharts1.cpp alone, and
`TestSharedCoreFixesQt()` (qttest.cpp:2880) needs 16 saved fields to
make one assertion about a midpoint.

*Incidents:* the suite's groups inherit each other's state (work log
item 57 — a group that passes alone and fails in the full run);
`FActionX()` half-restores what it scales and `FExportChartQt()` must
unconditionally restore four fields around every call (gotcha 3);
`gs.xWin` means "canvas plus sidebar" to some readers and "canvas" to
others, which walked saved windows 240px narrower per save/load cycle
(fixed, pinned in `TestSharedCoreFixesQt`).

*Direction:* not "eliminate globals" — that's the big bang this document
forbids. Three tractable moves, in order: **(1)** write down, per
struct, which fields are *settings* (user intent, serialized), which are
*derived* (recomputed by casting), and which are *scratch* — today all
three live interleaved in `US`; **(2)** give the save/restore dance one
home (a small push/pop helper or RAII guard per struct) so a forgotten
restore becomes grep-able; **(3)** make the worst offenders explicit
parameters where a function's whole contract is "reads two fields,
writes one" — findable case by case during area surveys, each one small.

*Cost/risk:* (1) is documentation, zero risk, high leverage — it is also
the prerequisite for T6. (2) is mechanical. (3) is open-ended; do it
opportunistically, never as a sweep.

### T2 — Index domains share `int`, and "none" is spelled three ways

*Evidence:* signs, objects, houses, aspects, rays are all `int`. Nothing
stops indexing a sign table with an object (13 entries vs 85). "None" is
`-1` in sign-keyed tables where `0` is a real object (the Earth), `0` in
object-keyed tables, and `-1` again in `rules[0]`-style slot-0 padding.

*Incidents:* the worst shared-core bugs this project found are all this
theme. Work log item 37: `if (k)` passing `-1` through to
`power1[-1] +=`, a stack write below an array, firing on any Esoteric
chart. Item 38: `ChartInfluence()` indexing 13-entry sign tables with
object numbers up to 84 — written by copying the traditional block and
swapping table names, exactly the mistake a type would have stopped.
Item 63: `rObjOrb[]`'s 85-slot initializer holding 84 anonymous values,
misaligning every object after Lilith *in the shipped defaults, for
years*.

*Direction:* the rgobjset conversion (item 63) and the rulership
cross-table assertion (item 64) are this theme's pattern: **named rows
for table initializers, and invariant assertions that machine-check the
encodings**. The next steps, cheapest first: **(1)** extend the
assertion approach to the remaining paired/parallel tables (exaltations,
ray tables — Area H will enumerate); **(2)** introduce documentation
typedefs (`typedef int sign; typedef int obj;`) used in signatures so a
reader sees the domain even though the compiler doesn't enforce it —
upstream-compatible, zero behavior; **(3)** only if (2) proves
insufficient, consider enum-class-style wrappers in a later, separate
decision — that one is invasive and needs the maintainer's call.

*Cost/risk:* (1) and (2) are cheap and safe. (3) is a real project;
don't drift into it.

### T3 — Two 1,000+-line switch statements are the command surface

*Evidence:* `FProcessSwitches()` (astrolog.cpp:1345, **1,774 lines**)
and `NProcessSwitchesRare()` (astrolog.cpp:485, 818 lines) parse every
`-X` style switch by hand: each case does its own `argc`/`FErrorArgc`
bookkeeping, its own `NParseSz` calls, its own field stores. On the
Windows side `NWmCommand()` (wdriver.cpp:1223, 1,301 lines) is the same
shape for menu commands. 47 functions in own code exceed 200 lines; 6
exceed 500.

*Incidents:* item 63 had to hand-split three parsers that selected a
target array by pointer across two index domains (`-YA`, `-Yj`, `-Yk`);
the `-W` case rejected by non-GUI builds used to make the console build
stop reading a settings file at that line (work log "what to do next"
item 8); the fork's settings-save fixes were all switches whose
parse existed but whose save-side twin didn't (T6).

*Direction:* the endgame is a **switch table**: name, argument count and
types, target field(s) or handler, help text — with parsing, arity
checking, and error reporting driven once from the table. That is a big
prize (it also mechanizes T6 and `DisplaySwitches()`'s help text, and
would have made item 63's parser splits trivial), and it can be
approached incrementally: first extract the per-case boilerplate
(`FErrorArgc` + `NParseSz` + range check) into helpers so cases shrink
to their essence, then migrate case families onto the table one letter
at a time, diffing `-H` output and the round trip after each. Area A/B
surveys own the concrete plan.

*Cost/risk:* medium per step, high total value. The table's shape should
be designed once, against the ugliest ten switches, not the easiest.
*(Amended by E2: the family is four parsers, four help printers, and
the settings writer — nine parallel descriptions of one surface.)*

### T4 — Serialization is a hand-written mirror of the parser

*Evidence:* `FOutputSettings()` (io.cpp:1392, 751 lines) writes the
settings file by `sprintf`-ing each switch by hand; nothing ties a line
it writes to the case that parses it. Every new switch needs its twin
added by hand, and forgetting is silent.

*Incidents:* the entire "settings the GUI drops" family this fork fixed
(work log item 8's sweep, and "Features this fork adds to both builds");
the `-YjT 84` bonus rows indexing `oNorm1 + 1` past the new struct —
found only because ASan happened to run (item 63); the spurious `-YD`
lines from the display-name convention (item 62).

*Direction:* this is T3's table again, read in the other direction —
when a switch is table-described, save is a loop. Until then, the
standing invariant is `tools/settings-round-trip.sh` (byte-identical
fixed point), which should grow a second leg: a settings file with
**every** switch family present, not just the maintainer's, so an
unsaved switch can't hide by being unset. That leg is a good early
increment for Area B.

*Cost/risk:* the round-trip extension is cheap and catches the whole
class. The table migration is T3's schedule.

### T5 — 1,300 unchecked `sprintf`/`strcpy` into fixed buffers

*Evidence:* ~1,310 calls across own code (211 in charts1.cpp, 210 in
io.cpp, 146 in intrpret.cpp...), nearly all into `char sz[cchSzMax]`
stack buffers. Last night's sessions retired the worst *duplication*
(three copies of the definition parser, five of the name lookup — items
59-62), but the underlying idiom — format into a fixed buffer, hope it
fits — is everywhere.

*Incidents:* item 24's three out-of-bounds fixes; the ASan catches of
items 37/63 were reads, but the same runs would have caught writes.
No confirmed overflow is currently known — ASan over the whole suite is
clean — which honestly caps this theme's urgency.

*Direction:* don't sweep 1,500 call sites. **(1)** keep ASan in the
pre-commit net (it is), **(2)** when an area survey touches a function,
prefer `snprintf`-shaped helpers the codebase already has conventions
for, **(3)** hunt specifically for the dangerous subset: `sprintf` into
buffers smaller than `cchSzMax`, and `%s` of user-controlled strings
(chart names, file paths, atlas rows) — that focused audit is one
session, Area D or G.

*Cost/risk:* the focused audit is cheap; the sweep would be pure churn
under an ASan-clean suite.

### T6 — Backend `#ifdef` interleave in the shared device layer

*Evidence:* the `x*.cpp` files are not X11 code but the shared renderer,
with `#ifdef X11 / WINANY / QT / WCLI` branches interleaved *inside
functions*: 52 conditionals in xgeneral.cpp, 44 in xscreen.cpp. A
backend is added by finding every branch point (the sweep is
`grep -lnE "ifdef QT|defined\(QT\)"`).

*Incidents:* the shape bugs hide in is a `WIN`-only branch with no QT
twin: items 39 and 54 (one was a plain value in a struct initialiser);
`FBmpDrawMap2()` crashing the process for exactly this omission (found
by the suite's first run); the port's whole existence is the record.

*Direction:* the classical fix is a device vtable — draw-line,
draw-text, set-color function pointers per backend — and `xgeneral.cpp`
is *already close*: most primitives bottleneck through a handful of
functions (`DrawLine`, `DrawEdge`, `DrawSz`...). The increment is to
push the remaining in-function `#ifdef`s down into those bottlenecks so
each backend is one block per primitive, not confetti. Full vtable only
if a fifth backend ever threatens. Area E owns the plan.

*Cost/risk:* medium; mechanical once the bottleneck list is verified
against all four backends. The Windows oracle diff is the net.

### T7 — Two rendering paths, selected by a global, restored by hand

*Evidence:* text (`Action()`→`PrintChart()`) and graphics
(`FActionX()`→`DrawChartX()`) are disjoint pipelines chosen by
`us.fGraphics`, with `gs.ft`/`gi.fFile` further multiplexing screen
versus file export inside the graphics path (QT_GUI_PLAN.md, "Two
separate rendering paths"). Every caller that wants "render this, but
elsewhere" mutates the globals and restores what it remembered to.

*Incidents:* gotcha 3's partial-restore trap; `RedrawTextQt()` exists
because text mode needed a temp-file capture of a path that only knows
how to stream to `is.S`; the chart-capture tools each re-derive the
same save/mutate/restore dance.

*Direction:* a render-target parameter is the endgame but touches
everything; the affordable step is **one blessed capture helper** in
shared code — "render current chart to this file/buffer with these
dimensions, touching nothing" — built once from the dance
`FExportChartQt()` already does correctly, then used by every capture
site (Qt export, test captures, Windows capture scenarios). Area E/G.

*Cost/risk:* low; it's consolidation of an idiom that exists five times.

### T8 — The conventions live in folklore, not in a file

*Evidence:* the Hungarian dialect (`f`/`n`/`r`/`rg`/`sz`/`ch`/`c`
prefixes, `Sav`, `pm*` parse modes), the CRLF rule, the two "none"
encodings, macro families like `SwitchF`/`inv`, and bare-word macros
(`META`, `PS`, `TIME` — which force Qt headers to be included before
astrolog.h). Each is learnable only from incident reports scattered
across three docs, or from being burned.

*Incidents:* the Qt-headers-first constraint cost real time (plan,
"How this fork's Qt backend works"); item 38's root cause was exactly a
convention (two none-spellings) that existed nowhere in writing until
this year.

*Direction:* one `CONVENTIONS.md` (or a section here), written during
the area surveys as each convention is *verified* rather than recalled:
naming prefixes, index domains and their none-values, buffer and error
idioms, macro hygiene rules, line endings. Cheap, immediate payoff for
every future session. The bare-word macros deserve their own small
finding in Area H: prefixing them (`fmtMETA`...) is upstream-divergent
but removes a whole class of include-order traps.

---

## Area findings

### Area A — the spine (astrolog.cpp), surveyed 2026-08-29

The file is main() plus program lifecycle (`InitProgram`,
`FinalizeProgram`, `InitVariables`, `InitColors`, `InitRestrictions`),
the dispatcher `Action()`, and the two switch parsers of T3. Findings
beyond the themes:

**A1 — The rare/common parser split is historical, not semantic.**
`NProcessSwitchesRare()` handles `-Y*` ("rare") switches, 818 lines;
`FProcessSwitches()` everything else, 1,774. The split forces shared
helpers (`FErrorArgc`, argument advance) to be duplicated in shape, and
a switch's home is decided by its *spelling*, so related settings live
850 lines apart (`-j` influences in one, `-Yj` influence tweaks in the
other). *Direction:* fold into one table when T3 lands; until then,
treat the pair as one unit in any edit. *Cost:* none now — this finding
is a constraint, not a task.

**A2 — Each case hand-rolls arity/advance, and the failure is silent
truncation of the file.** A case that consumes `i` arguments must
remember both `FErrorArgc("X", argc, i)` *and* `argc -= i; argv += i;`.
Getting the pair wrong misparses everything after it — the exact
mechanism that made the console build stop reading settings files at
`-W` (to-do item 8's tail). *Incident:* that, plus item 63's parser
splits. *Direction:* first T3 increment lives here: a
`consume(n)`-style helper pair so arity and advance can't diverge; it
shrinks every case by two lines and makes the eventual table migration
diffable. *Cost:* low, mechanical, high fan-out — needs the round trip
plus `-H` diff as net.

**A3 — `Action()` is a second dispatcher with its own mode zoo.**
astrolog.cpp:141 (~200 lines): the "-tq means transit listing unless
-g..." style else-if chain re-derives what kind of run this is from
combinations of `us.f*` flags, in an order that matters, duplicated in
spirit by the Qt port's chart-mode table (`rgchartmodeQt[]`) and
Windows' `ProcessState()`. Three places know the flag→chart-kind
mapping. *Incident:* gotcha 4 — `DetectGraphicsChartMode()` (the fourth
knower) has real gaps that blank-charted several modes on Windows'
zero-and-redetect pattern. *Direction:* one shared flag↔mode table in
the core (the Qt port's table is the model; item 13 built it for
sync already), consumed by `Action()`, `DetectGraphicsChartMode()`, and
the ports. *Cost:* medium; behavioral risk concentrated in else-if
order, which the 26 chart-render assertions and Windows text-diff
cover.

**A4 — Program lifecycle assumes one shot, GUI runs it forever.**
`InitProgram()`/`FinalizeProgram()` and friends were written for
parse-cast-print-exit; the GUI loops inside what was a straight line,
which is why first-use caches (`SwissEnsurePath()`'s ephemeris path,
item in CLAUDE.md re `-Yi1`) and half-restored scaling (gotcha 3) bite
only interactive runs. *Incident:* the `???` esoteric bodies without
`-i nrvate.as`; startup diagnostics needing their own out-of-process
test section (plan item 27). *Direction:* document the lifecycle
contract (what is idempotent, what caches, what must precede what) in
this file as Area A completes; actual restructuring is low priority
while the contract is written down. *Cost:* documentation now.

**A5 — `main()` is duplicated per platform by `#ifdef`** (astrolog.cpp
:3474 vs :3477) with WIN taking `WinMain` in wdriver. Minor; note only
so Area G reconciles how the Qt build enters. *Cost:* trivial;
fold when touched.

### Area B — settings & serialization (io.cpp), surveyed 2026-08-29

The file is six file-format parsers (switch/.as, AAF, quick, ADB,
Solar Fire text, calendar), their output twins, the shared value parsers
`NParseSz`/`RParseSz`, chart-info input, and the fork's HTTP client.
One seeded suspicion dissolved on reading: the 51 `goto`s are almost all
a disciplined single-exit `goto LDone` cleanup idiom — the right shape
for C-style resource handling, to be documented (T8), not refactored.

**B1 — Six parsers, three hand-rolled line readers, visibly drifting.**
`FProcessSwitchFile` (io.cpp:234) reads via `getbyte()` with
realloc-doubling growth; `FProcessAAFFile` (io.cpp:449) hand-loops
`getc` into a fixed `cchSzLine` buffer, silently truncating; the Solar
Fire and calendar parsers call `fgets(szLine, cchSzMax, ...)` into
buffers declared `cchSzLine` — **four times larger than the read limit**
(io.cpp:972/987, 1108/1123), so a long line silently splits at 254
characters while the buffer says 1020 was intended. No incident yet;
the drift is the evidence. *Direction:* one line-reader helper with an
explicit growth/truncation policy per caller — but first pin current
behavior with long-line fixture tests, because the existing truncation
points may be load-bearing for real files. *Cost:* low; net first.

**B2 — Virtual filenames are in-band magic strings.** `FInputData`
(io.cpp:2656) special-cases the names `nul`, `set`, `now`, `tty`,
`__t`, `__g`, `__d`, `__1`..`__6` before touching the filesystem — so
a real file named `now` is unreachable, and the full list exists only
as an if-chain. *Direction:* keep the behavior (scripts depend on it);
lift the names into one commented table the if-chain walks, and list
them in the helpfile section that documents `-i`. *Cost:* trivial.

**B3 — The switch-file channel is a global, and nested includes clobber
it.** Parsers publish their `FILE*` in `is.fileIn`, and the atlas/zone
loading switches (astrolog.cpp:1273-1290) read *the rest of the current
file* through that global — a command file is really a switch stream
with an in-band binary payload, multiplexed by global state. Every
parser NULLs `is.fileIn` at its `LDone` unconditionally, so an `-i`
include nested inside a file leaves the *outer* file's channel NULL;
an atlas-load switch after the include point would fail. Latent — no
incident, the bundled files never nest that way. *Direction:* save and
restore the previous value around each parser (three-line fix), plus a
regression test with a nested include followed by a payload switch.
*Cost:* low, and it's the kind of quiet misdesign this document exists
to catch before it draws blood.

**B4 — Chart info hides behind two-letter field macros.** `MM`, `DD`,
`YY`, `TT`, `SS`, `ZZ`, `OO`, `AA` (extern.h:83-90) alias `ciCore`
fields, so `GetTimeNow(&MM, ...)` mutates a global through what reads
as a local. Pervasive and upstream-idiomatic — renaming is churn (see
non-goals) — but it belongs in the conventions doc, and new code should
write `ciCore.mon` explicitly. Tagged T8. *Cost:* documentation.

**B5 — T4's concrete increment lives here.** Extend
`tools/settings-round-trip.sh` with a second leg: a fixture settings
file exercising **every** switch family `FOutputSettings()` writes, so
a switch whose save-twin is missing cannot hide by being unset in the
maintainer's config. *Cost:* one session, pure test.

### Area C — computation core (calc.cpp, matrix.cpp), surveyed 2026-08-29

matrix.cpp gets a clean verdict: it is the oldest code and a coherent
single-purpose backend (the built-in "Matrix" math), reached only
through dispatch fronts like `MdyToJulian()` (calc.cpp:66) that pick
Matrix/Placalc/Swiss per call. Leave it alone. Credit also to the
`FCm*` backend predicates (extern.h:140-147) — the capability tests are
already single-homed — and to `CP` being a real struct rather than
parallel arrays. The findings:

**C1 — calc.cpp is three modules sharing a file.** (a) Chart math —
houses, aspects, eclipses, `CastChart`/`ComputeEphem`; (b) a ~950-line
Swiss Ephemeris wrapper layer (`SwissEnsurePath` through `SwissRevJul`,
calc.cpp:2453-4000); (c) the fork's object-selection/OBJDEF store and
parsers (calc.cpp:2642-3038), which are naming/settings code, not
computation, and landed here mainly to stay `#ifdef`-free for both
builds. *Direction:* low urgency; if the OBJDEF code grows again, give
it its own file in both Makefiles. Until then the boundary is
documented here. *Cost:* deferral is free.

**C2 — `CastChart` cooks the user's input in place.** It normalizes
`ZZ`/`SS`/`TT` (LMT/LAT zones, auto-DST, zone-into-time folding)
directly inside `ciCore` — the same storage holding what the user typed
— and restores from a stack copy 350 lines later (calc.cpp:1228→1584).
Verified: no early return currently sits inside the window, but any
future one silently corrupts the chart info, and every reader of
`ciCore` must know whether it is currently "typed" or "cooked".
*Incident:* none recorded — the discipline has held; that is luck, not
design. *Direction:* derive into locals or a working copy handed to the
house/ephemeris calls; needs care because downstream code (Matrix
`ProcessInput()`) reads the cooked values through the same macros. Pin
with a zone/DST/LMT round-trip test first. *Cost:* medium; net first.

**C3 — `ComputeEphem`'s skip predicate is write-only logic.** The
decision "compute this object or not" is six OR'd clauses mixing four
concerns — restriction policy, object taxonomy, computation center,
backend capability (calc.cpp:991-999). *Incident:* the forced-midpoint
bug lived exactly here — restricted objects a forced midpoint depends
on weren't computed — and its fix (`FObjMidSource`) made the predicate
longer still (work log; pinned by `TestSharedCoreFixesQt`).
*Direction:* name the clauses: small predicates or a per-object
skip-reason value, which also makes "why is this object blank?"
diagnosable at runtime. Behavior-preserving by construction; the suite
plus a Windows text-diff is the net. *Cost:* low, good early increment.

**C4 — The backend selector is three flags and an int with unreachable
corners.** `fEphemFiles`, `fPlacalcPla`, `fMatrixPla`, `fMatrixStar`,
`nSwissEph` 0-3 jointly encode one choice (astrolog.h:1806-1820);
illegal combinations are representable, meaning lives in negations
("ephem files but not Placalc" = Swiss), and each math entry point
dispatches longhand. *Direction:* document the reachable state space
(which switch sequences produce what); collapsing to one derived enum
is worthwhile but belongs to the T3 table work, since the flags are set
by switch parsing. *Cost:* documentation now, enum later.

**C5 — The Swiss boundary translates object numbers by offset
arithmetic and caches by stealth.** `FSwissPlanet()` (calc.cpp:3055+)
maps three numbering domains (Astrolog objects, SE constants, custom
slots via `SE_*_OFFSET` bases) in one if-chain — T2 in its purest form,
though at least single-homed — and `SwissEnsurePath()` plus a static
`nSwissEph` change-detector cache path state invisibly. *Incident:* the
cached ephemeris path is why every test must run `-i nrvate.as` or read
`???` for esoteric bodies — a hard rule in CLAUDE.md exists because of
this cache. *Direction:* fold the cache contract into A4's lifecycle
doc; consider a mapping table when next touched. *Cost:* low.

**C6 — Chart positions are seven global slots with macro aliases.**
`planet` *is* `cp0.obj` (extern.h:110); rings `cp1`..`cp6` are copied
around wholesale by relationship modes. *Incident:* relationship charts
losing their mode on recast needed a fork fix and a standing test
(`TestRelationshipModeQt`). Mostly T1; the increment is to document
slot ownership — who writes each ring and when — as the surveys reach
the chart code that juggles them (Area D). *Cost:* documentation.

### Area D — text charts & interpretation, surveyed 2026-08-29

charts0.cpp is the help/table displays and the print primitives;
charts1.cpp the single-chart listings and wheels; charts2.cpp the
relation (two-chart) variants; charts3.cpp the time searches;
intrpret.cpp the interpretation engine. Two clean verdicts first: the
interpretation engine is genuinely data-driven — phrase tables
(`szMindPart`, `szInteract`, `szModify`, `szTherefore`) composed by a
word-wrap accumulator — a design to preserve, not fix; and the two big
search loops (`ChartInDaySearch`/`ChartTransitSearch`) turn out to
share machinery, not lines (~23% textual overlap; the shared parts —
`InDayInfo`/`TransInfo`, `PrintInDays` — are already factored out).
Leave both alone.

**D1 — Eight single/relation chart clone pairs.** `ChartListing`,
`ChartGrid`, `ChartAspect`, `ChartMidpoint`, `ChartAstroGraph` each
have a `...Relation` twin in charts2.cpp, and `InterpretAspect`/
`InterpretGrid`/`InterpretMidpoint` repeat the pattern. Measured on the
aspect pair: 118 vs 113 lines, ~70% identical — same loop, same
sorting, same summary, differing in grid source and iteration domain.
*Incident:* the clone-and-swap culture is what produced item 38
(traditional block copied, table names swapped, wrong domain), and
relationship-mode state needed a fork fix (`TestRelationshipModeQt`).
*Direction:* merge pair by pair into one core taking the position
sources and iteration domain as parameters — and this area has the best
net in the project: `tools/text-chart-capture.sh` +
`text-chart-diff.py` byte-diff every text chart against the Windows
build. A pair a session, diff after each. *Cost:* medium, mechanical,
high confidence.

**D2 — The influence stanzas are twelve-line clones over table names.**
`ComputeInfluence()` (intrpret.cpp:1319-1346) repeats
`k = table[j]; if (k > 0 && i != k) power1[k] += x;` twelve times per
concern across traditional/esoteric/hierarchical, differing only in
which table — with real guard subtleties between blocks (`i != k`
present in the sign loop, absent in the house-cusp loop; `rules[]`
unguarded because its entries are never empty). *Incident:* items 37
and 38 — the two worst memory bugs found in this project — were both in
these stanzas. *Direction:* iterate the same three-system family table
the `rulership` suite group already models, preserving each block's
guard semantics exactly; the data side is pinned by item 64's
assertions, the behavior side by the suite and Windows text-diff.
*Cost:* small; good early increment.

**D3 — The print pipeline is modal global state.** Output goes through
`is.S` redirection; `AnsiColor()` emits ANSI escapes or HTML `<font>`
tags depending on `us.fAnsiColor`/`us.fTextHTML` (which
`RedrawTextQt()` deliberately exploits); column layout is hand-counted
spaces, so a one-character label change silently breaks alignment; and
`PrintS()` (charts0.cpp:177) colorizes the help screen by *parsing the
help text's own characters* with static cross-call state. `FieldWord()`
likewise accumulates in a static buffer flushed by a `NULL` call.
*Verdict:* document the modes and the flush conventions (T8) and rely
on the text-diff tooling as the net for any layout-touching change;
none of it is worth restructuring on its own. Tagged T1/T8.

**D4 — `PrintChart()` is the text twin of A3's mode zoo.**
charts1.cpp:2715 dispatches on the same `us.f*` flag combinations
`Action()`, `DetectGraphicsChartMode()` and the ports each re-derive,
in an else-if order that matters (the context menus mirror it, work
log item 1). Tagged onto A3: one shared flag↔mode table serves all
four consumers. *Cost:* counted under A3.

### Area E — graphics core & device layer, surveyed 2026-08-29

xgeneral.cpp is the drawing primitives; xscreen.cpp the window/event
lifecycle, graphics switch parsing, and `FActionX()`; xdevice.cpp the
file-format writers (BMP/PNG/XBM, PostScript, metafile, SVG, wireframe)
plus bitmap operations; xdata.cpp is graphics globals and data tables
(verdict: it's data, leave it). The good news first: **T6 is in better
shape than assumed** — the backend branches concentrate *inside* ~11
primitives (`DrawColor`, `DrawPoint`, `DrawBlock`, `DrawDash`,
`DrawArc`, `DrawEllipse2`, `DrawFill`, `DrawSz`, `DrawGlyph`,
`DrawClearScreen`, `DrawThick`); everything above them, all of
xcharts*, is target-free. The device layer exists; it's just written
longhand.

**E1 — Each primitive multiplexes two axes at once.** Screen backend
(X11/WINANY/QT, compile-time) and file format (`gs.ft` = PS/WMF/SVG/
wire at runtime, under `gi.fFile`) interleave in each primitive body:
`DrawColor()` (xgeneral.cpp:75) handles seven targets in one function,
and `DrawSz()` scatters seven conditional blocks. Some primitives are
already clean one-block-per-target (`DrawBlock`); others grew organic.
*Incident:* the missing-QT-branch crash class (items 39/54,
`FBmpDrawMap2()` leaving `bmp` on a never-allocated buffer — the
suite's first-run catch). *Direction:* normalize every primitive to
one block per target in a fixed order — mechanical, verifiable by the
`QTGRAPHDIR` captures and Windows comparison; a true target vtable
(one struct of pointers per screen backend *and* per file format)
becomes a small step afterward instead of a leap, and would let a new
export format be one file instead of edits to eleven functions.
*Cost:* normalization low; vtable medium, only if warranted.

**E2 — T3's parser family is four, not two.** `NProcessSwitchesX()`
(xscreen.cpp:1354, ~480 lines) and `NProcessSwitchesRareX()`
(xscreen.cpp:1835, ~340 lines) parse the graphics switches, with their
own help twins (`DisplaySwitchesX`/`DisplaySwitchesW`, charts0.cpp).
The eventual switch table must cover all four parsers, the four help
printers, and `FOutputSettings()` — the full count of hand-kept
parallel descriptions of the switch surface is **nine**. T3 amended.

**E3 — Four event loops own the same keystroke surface.** X11 and WCLI
share `InteractX()` (xscreen.cpp:629, itself internally `#ifdef`-split);
Windows has `WndProc()`; Qt runs its own loop. The keystroke→command
mapping already unified once through the resource-generated accelerator
work (item 2); the loops themselves are inherently per-backend and stay.
*Direction:* document ownership (which loop serves which build, where a
new command must be added) in T8's conventions doc; no restructuring.
*Cost:* documentation.

### Areas F-H — pending

---

## Done

- **T2 partial** — rulership cross-table invariant asserted
  (qttest.cpp `rulership` group), commit `b36415b`, 2026-08-29.
- **T2/T4 partial** — per-object settings struct `rgobjset[]` with named
  rows; settings round-trip script. Commit `caf3205` (work log item 63).
- **T5 partial** — definition parse/format unified, name lookups folded
  (items 59-62), commits `9218317`..`9c83003`.
