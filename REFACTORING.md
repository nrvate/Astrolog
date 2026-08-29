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
| B. Settings & serialization | io.cpp (3211) | pending |
| C. Computation core | calc.cpp (4000), matrix.cpp (703), ephemeris glue | pending |
| D. Text charts & interpretation | charts0-3.cpp (8876), intrpret.cpp (1605) | pending |
| E. Graphics core & device layer | xscreen.cpp, xgeneral.cpp, xdevice.cpp, xdata.cpp (8971) | pending |
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

### Area B — settings & serialization (io.cpp) — pending

Seeded from T4: the full-coverage round-trip leg; the 51 `goto`s in
io.cpp (highest in the codebase) want a look at the error-path idiom.

### Areas C-H — pending

---

## Done

- **T2 partial** — rulership cross-table invariant asserted
  (qttest.cpp `rulership` group), commit `b36415b`, 2026-08-29.
- **T2/T4 partial** — per-object settings struct `rgobjset[]` with named
  rows; settings round-trip script. Commit `caf3205` (work log item 63).
- **T5 partial** — definition parse/format unified, name lookups folded
  (items 59-62), commits `9218317`..`9c83003`.
