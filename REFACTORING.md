# The standing refactoring review

This is the catalog of what makes Astrolog hard to evolve and what to do
about it. The goal is a codebase someone could read without cringing:
maintainable, flexible, modular — while every increment keeps the program
byte-for-byte behaving as it does today, under the nets this project
already trusts (the 3036-assertion suite, ASan, the settings round trip,
the six standing audits, and the Windows build as oracle).

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
| F. Graphics charts | xcharts0-2.cpp (9129) | **surveyed 2026-08-29** — findings F1-F4 |
| G. Frontends & satellites | qtdriver/qtdialog (9345), express.cpp (2936), atlas.cpp (2171) | **surveyed 2026-08-29** — findings G1-G4 |
| H. Data model & headers | astrolog.h (2457), extern.h (1243), data.cpp (1702) | **surveyed 2026-08-29** — findings H1-H3 |

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

**Move (1) done 2026-08-29** (work log item 92), with a finding that
sharpened it: the struct split already *is* the classification — US/GS
are user intent, IS/GI are derived and scratch, and shadowed names
(us.fProgress = the request, is.fProgress = what the cast did) mark the
boundary. The interleaving problem is not misfiled fields but
computation *borrowing* settings fields via the *Sav dance. The
contract now sits on all four struct heads in astrolog.h.

**Move (2) done 2026-08-29** (work log item 94): the `Borrow` scope
guard in astrolog.h is the one home for the dance; CONVENTIONS.md
carries the usage rule (new code borrows, old `*Sav` sites convert
opportunistically). Two exemplars converted: the settings writer's
two brace-scoped borrows (io.cpp; written file byte-identical) and
gotcha 3's print path (qtdriver.cpp), whose two hand-written restore
blocks collapsed into one closing brace — with the out-of-memory
warning moved after the restore so the modal repaint cannot happen at
print scale, T7's exact incident shape. The console `Makefile` now
links with g++: the guard's destructor is the first thing in the
shared core that needs the C++ EH runtime. Move (3) stays
opportunistic, per the non-campaign policy.

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

#### The way off the switch hacks — T3's design, promoted 2026-08-29

Promoted at the maintainer's direction: the fragility itself is the
target, not its symptoms. The diagnosis in one line: **the switch
surface is this program's only API** — menus, dialogs, settings files,
macros, and AstroExpressions all compile down to switch strings — and
that one surface is maintained as nine hand-written parallel
descriptions, with side channels (`is.fileIn`) and packed encodings
(`-Yu`'s suffix bit, `-v3`'s value-boolean) living only in folklore.
Every bug increments 1-3 caught is this one disease presenting nine
ways.

**The destination: one switch registry.** A compiled-in table, one row
per switch, that every consumer derives from:

    typedef struct _switchdef {
      CONST char *szName;   // "YQ" -- the name after the dash
      word grf;             // persistent? file-context payload? which builds?
      byte carg, cargMax;   // arity, min and max
      byte rgpm[4];         // parse mode per argument (pmObject, pmLength...)
      void *pv; int pt;     // target field and its type, or --
      int (*pfn)();         // -- a handler, for the genuinely weird
      CONST char *szHelp;   // the -H line
    } SWITCHDEF;

Everything the nine descriptions do becomes a derivation:

1. **One parse driver** owns prefix handling (`=`/`_`/`:`/`-`), arity
   checking, `NParseSz` by declared mode, range checks, the store, and
   the argv advance. Roughly 80% of switches become pure rows; the
   weird 20% keep a handler but lose all their boilerplate. A2's
   arity/advance drift becomes impossible.
2. **`FOutputSettings()` becomes a loop** over rows marked persistent,
   formatting the current value through the same descriptor that parses
   it. Save and load are inverses *by construction*; the round-trip
   legs stop being able to fail for migrated rows. T4 dissolves.
3. **`-H` help is printed from the rows** — `DisplaySwitches*()`
   retired after a one-time diff against their old output.
4. **The audits read the table** instead of regexing C source —
   `defaults_audit` gains exact coverage, and a new audit becomes
   possible: every dialog control maps to a row (rc_field_audit's
   missing other half).
5. **Packed encodings get declared.** A row whose state doesn't fit
   "one field" carries an encode/decode pair — the `-Yu` suffix bit
   and `-v3`'s value-boolean become named code instead of trivia that
   bites once a year.

**The channel dies with it.** The driver threads a parse context —
`{FILE *fileIn; CONST char *szSource; int iLine;}` — through parsing,
so `is.fileIn` disappears, payload switches declare themselves in
their row, error messages finally say *file and line*, and a bad
switch can skip-and-continue instead of silently truncating the rest
of the file (A2's failure mode, gone for good).

**Migration, each step shipping green under the nets built this week:**

- **M1**: registry, driver, and parse context; migrate *one ugly
  family* end to end to prove the row shape against the worst cases —
  `-YJ` (multi-domain name parsing plus the `AdjustRulership` side
  effect) and one ranged-row family (`-Yj`/`-YAm`). Includes the -H
  diff harness. This is the design-heavy step.
  **Done 2026-08-29** (work log item 68), scoped to registry + driver +
  fifteen switches (the -YA*, -Yj*, -YJ* families); the parse context
  is deferred to the family that needs it (-YY) rather than shipped as
  dead structure. Proven by a 26-invocation old-vs-new binary
  differential, byte-identical. The differential itself exposed that
  `sprintf2` was unbounded on non-Windows builds — fixed, see T5.
- **M2..Mn**: one switch letter-family per session, deleting each old
  case as it migrates. After each: the suite, all three round-trip
  legs, a `-H` diff, the defaults audit, and a Windows text-chart
  diff.
  **M2 done 2026-08-29** (work log item 69): the ranged descriptor
  gained value kinds and a post-store hook; -YR/-YRT, -Y7O/-Y7C and
  the six -Yk* families are rows, fifteen sub-switches are handlers,
  and cases 'R', '7', 'k' are deleted. 39 switches now live in the
  registry; the 61-invocation differential is byte-identical (redone
  against the real code after the stash/worktree incident the commit
  history records).
  **M3 done 2026-08-29** (work log item 70): the parse context landed
  and `is.fileIn` is deleted -- payload switches read the file being
  parsed through an argument, not a global. ~60 switches registered;
  prefix rows cover -Ye's spelling-embedded parameters; eight more
  cases deleted; 96-invocation differential byte-identical.
  **M4 done 2026-08-29** (work log item 71): the rare parser is
  deleted. Flag rows joined the row shapes; every -Y switch is
  registry-resident (with a -YX bridge to the graphics parser);
  156-invocation differential byte-identical. ~95 switches migrated.
  **M5 done 2026-08-29** (work log item 72): NProcessSwitchesRareX()
  deleted -- the second of the four parsers gone. The -YX graphics
  family is registry-resident; 192-invocation differential
  byte-identical; ~120 switches migrated.
  **M6 done 2026-08-29** (work log item 73): NProcessSwitchesX()
  deleted -- three of four parsers gone. The rows gained a `grf` bits
  field whose first use declares the -X family's lockdown-and-enable
  behavior once instead of coding it around a call site. ~160 switches
  migrated; 259-invocation differential byte-identical.
  **M7 done 2026-08-29** (work log item 74): the -R/-C/-u/-U/-A
  restriction and aspect cluster left the main parser; ~175 switches
  migrated; 314-invocation differential byte-identical.
  **M8 done 2026-08-29** (work log item 75): the chart-computation
  letters (-b/-c/-s/-h/-p/-x/-1/-2/-3/-4/-9/-f/-G/-J/-F) left the main
  parser; a `nSwitchStop` sentinel lets a row end parsing successfully
  (the WIN screensaver quirk). ~190 switches migrated; 377-invocation
  differential byte-identical. What remains: chart types, chart info,
  I/O and macros, day arithmetic, and -W.
  **M9 done 2026-08-29** (work log item 76): the twenty-four
  chart-type letters left the main parser; 460-invocation differential
  byte-identical. What remains: chart info (-n/-z/-q/-i), I/O and
  macros, relationship charts (-r*), day arithmetic, -H* help, and -W.
  **M10 done 2026-08-29** (work log item 77): the main parser's switch
  statement is deleted. FProcessSwitches() is 44 lines; every spelling
  is registry-resident. The migration phase of T3 is complete -- what
  remains of T3 is the harvest: the table-driven settings writer (T4),
  generated -H help, and audits reading the registry.
  **Harvest step 1 done 2026-08-29** (work log item 78): the registry
  is self-checking (suite group `registry`, structural invariants) and
  `tools/registry_audit.py` holds the -H text and the settings writer
  to it -- 449 spellings resolve; its first run found and fixed -YYI,
  documented but dead behind a misspelled ifdef since upstream wrote
  it.
- **M-final**: ~~the four parser shells reduce to dispatch loops~~ —
  **happened 2026-08-29** (M10, work log item 77): the shells aren't
  reduced, they're *gone*. The help printers and `FOutputSettings()`
  restructure remain as harvest work, below.

**Honest scale**: ~200 switches; M1 is the hard thinking; the family
migrations are a session each, ~10-15 sessions total at that pace, and
the surface stays fully functional at every intermediate commit. What
does *not* change: the `.as` file format (existing files stay valid
forever), the command line, and Windows parity — the same table
compiles into both builds.

What it ends, structurally rather than one bug at a time: missing
save-twins, arity/advance drift, packed-bit asymmetries, channel
clobbering, and silent file truncation — the five classes this week's
increments each caught one instance of.

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
items 37/63 were reads. The caveat this section used to carry — "no
confirmed overflow is currently known" — died 2026-08-29: increment 2's
flag-flip leg crashed the fortified console build in `SzLocation()`
(`szLoc[25]`, 29 bytes needed under `=b1`), and `SzZodiac()`'s
expression branch overflowed `szZod[16]` on every call. Both fixed
(item 66). The suite's ASan runs never see these because no test sets
`=b1`; T5's focused audit of small static buffers under maximal
formats is now *evidenced*, not speculative. And it got worse before
it got better: M1's differential exposed that `sprintf2`/`S()`/`SO()`
— the codebase's own bounded-write convention — were only bounded
`#ifdef PC`, so *every* such call was plain `sprintf` on non-Windows
builds, and a deep install directory crashed startup in `FileOpen()`'s
path probing. The snprintf branch is unconditional now (item 68).

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

**Done 2026-08-29** (work log item 91): `CONVENTIONS.md` exists at the
repo root, every entry verified against the code with references. It
absorbs the documentation halves of B4 (chart-info aliases), E3 (loop
ownership and adding a command), G3 (label-is-an-identifier), H1's
taxonomy note, H2's macro-prefix rule, and the B-area goto/flush
verdicts.

---

## The registry as built — read this before touching switch code

*(Written 2026-08-29 at the end of the migration day, for the next
session's fresh context. Everything here is verified fact, not plan.)*

**Where everything lives.** All in switch.cpp (extracted from
astrolog.cpp by P1, 2026-08-29): `rgswflag[]` (flag
rows: name + `flag *`, prefix semantics applied by the dispatch),
`rgswranged[]` (ranged setters: index domain/bounds, value kind
`vtReal/vtBool/vtRay/vtColor` with per-kind validation and error
conventions, target slot + stride, post-store hook), and
`rgswitchdef[]` (handlers: `{name, grf, pfn}`). Dispatch is
`NProcessSwitchTable()`, scanning flags, then ranged, then handlers in
row order; handler rows match exactly, or as a *prefix* when
`grfSwPrefix` is set (the handler receives the full spelling and scans
its suffix — `-Ye`, `-t`, the whole chart-type family work this way).
`grfSwGraphics` declares the -X family's lockdown-and-enable wrap.
Handlers return args consumed, `tcError`, or `nSwitchStop`
(success-and-stop: `-;`, and the lockdown paths whose old
`return tcError` from FProcessSwitches read as success).
`nSwitchAbsent` means unknown switch — nothing falls through anymore;
`FProcessSwitches()` is 44 lines. `PARSECTX` (astrolog.h) carries the
switch file's `FILE*` down for payload switches (-YY family);
`is.fileIn` no longer exists. `FSwitchRegistryRow()` enumerates all
260 rows for the suite's `registry` group and tools.

**Invariants that are enforced, so rely on them:** spellings unique;
no prefix row shadows any row scanned after it; exactly one empty
spelling (day arithmetic). The suite group and
`tools/registry_audit.py` (help text and settings writer vs registry,
449 spellings) fail on drift.

**Adding a switch now:** one row (+ handler if not a flag/ranged
shape), its `-H` line, and its `FOutputSettings()` line if persistent —
and the registry audit reminds you of the last two.

**The verification method for any behavior-touching change:**
1. Baseline binary: `git worktree add DIR <commit>`, build there, copy
   the binary out, remove the worktree. Never stash around this
   (worktree add checks out its commit regardless of the dirty main
   tree; a stash pop obeys cwd and once destroyed uncommitted work —
   work log item 69's commit message).
2. `tools/switch-matrix.sh` (529 invocations, parser surface) and/or
   `tools/influence-matrix.sh` (computation), old vs new, byte-diffed.
   Extend the matrix with the touched family's valid/edge/error shapes
   *before* trusting it; a green diff over output that's implausibly
   short is a broken harness, which has happened twice (a stray token
   parsed as a switch; -q's four-argument arity).
   An ad-hoc differential (positions, a chart mode the matrices don't
   cover) must copy `run()`'s discipline from `switch-matrix.sh`
   verbatim: `env -u DISPLAY`, `</dev/null`, `timeout`, and the `_X`
   token — `nrvate.as` switches graphics *on*, so a console run
   without `_X` pops an X window on the real desktop. Forgetting this
   once interrupted a session (C3's first differential attempt).
   Two more, both found by D1's distinctness check: `nrvate.as`
   already toggles chart flags (`-ao` line 281), so an ad-hoc case
   must use the `=` force-on prefix or the switch silently toggles
   the chart *off* and every case diffs green over the default -v
   listing; and `grep`/`grep -c` on chart output silently prints
   nothing without `-a` (ANSI escapes plus degree bytes trip binary
   detection). After building any case list, prove the cases differ
   from each other (`cmp` each against the base case) before trusting
   a green diff.
   For graphics code, `-Xb -Xo file.bmp` renders any chart to a
   bitmap headlessly (no display, no window manager, no Qt harness):
   a pinned-date `-Xb` differential is the graphics analogue of the
   text matrices, and F1's 12-case run is the worked example.
3. The battery: qt/win/console builds, `./run-qt-tests.sh` (3039/0),
   `tools/settings-round-trip.sh` (three legs), `defaults_audit.py`,
   `registry_audit.py`, then ASan suite and `tools/win-tests.sh` —
   those last two in parallel subshells, they're the slow tail.
4. Doc edits and `git commit` must be chained on exit status (`if
   [ $? -eq 0 ]`), never sequential statements — a failed edit script
   shipped a half-commit twice before this rule.

**Strictness policy, settled:** exact/flag-row migrations may turn
garbage-suffix aliases into unknown-switch errors (documented per item
in work log 68-77); prefix rows are provably equivalent to the old
cases for every token, so prefer them wherever the old case read its
suffix. Error *messages* for things that were always errors may
reword; valid input behavior is byte-sacred.

**Harvest state and constraints:**
- Done: registry self-check + cross-audits (item 78, which also
  revived -YYI from upstream's misspelled `#ifdef INTRPRET`).
- `FOutputSettings()` as a row-driven loop: **closed as measured
  infeasible-at-value, 2026-08-29** (work log item 87). The
  constraint stands (byte-stable user-facing layout), and the
  single-sourcing idea was measured: of the 42 simple flag emissions
  (`ChDashF` lines), only 9 spellings are exact registry flag rows —
  the other 33 are suffix-parsed inside prefix handlers ("b0" inside
  NSwb, the -X family, the -W passthroughs), where no row lookup can
  reach without exploding the registry into per-suffix exact rows,
  which the migration deliberately avoided. `registry_audit.py`
  already closes the spelling-drift class; the value-drift class
  stays covered by the round-trip fixture's 31 sentinels. Nothing
  further to harvest here.
- Generated `-H` help: **closed as measured no-gain, 2026-08-29**
  (work log item 88). The help lines are pedagogical prose with
  many-to-one structure — `_a[jonOPACDm]` documents nine rows in one
  line plus a continuation, section headers interleave, and ifdef'd
  composites are sprintf-built — so a generated version degenerates
  into an ordered (guard, string) list, which is exactly what the
  PrintS sequence already is. `registry_audit.py` already parses
  these lines and verifies every documented spelling resolves to a
  row, so generation would add no drift protection. **T3 is now
  fully closed**: migration (M1-M10), hardening (suite group +
  cross-audits), and both harvest ideas measured and closed.

**Next up, specified:**
- **C3 — done 2026-08-29** (work log item 81): `FSkipEphem()` in
  calc.cpp, five sequential ifs each carrying its verified reason
  (the JPL-Earth clause's reason was checked against `rgObjJPL[]`
  before writing it down, and came out different from the first
  guess). Net: 11-case `-v` position differential old-vs-new under
  restriction/backend combinations, byte-identical; suite 3036/0;
  win cross-build.
- **D1 first pair — done 2026-08-29** (work log item 82): static
  `ChartAspectCore(flag fRel)` in charts1.cpp; both public names are
  wrappers, the charts2.cpp copy is deleted. Net: 32-case old-vs-new
  differential (both lists x all nine sort keys x p/d/a/x variants,
  summary, interpret), byte-identical. Two harness footguns joined
  the recipe below.
- **D1 second pair — done 2026-08-29** (work log item 83): the
  midpoint lists likewise, as `ChartMidpointCore(flag fRel)` in
  charts1.cpp. 16-case differential (=m/=m0/=ma, interpret, 3D
  house, -RO, parallel — parallel needed `=ap _a` to force, -Yp was
  the wrong switch), byte-identical.
- **D1 survey verdict, 2026-08-29** (work log item 84): the aspect
  and midpoint pairs were the mechanical wins and are done. Of the
  rest, read closely: the Listing and Grid twins are *different
  layouts*, not clones (multi-chart columns + delta vs. houses and
  rulerships; axis-labeled matrix + header band vs. diagonal square)
  — merging them would be mostly `if (fRel)` noise. The Interpret
  trio are cousins with genuinely different prose ("their X" vs.
  naming person 2, a conjunction-only fallback, a dropped life-area
  tail) — a merge trades duplication for conditional-text soup; skip
  all three on purpose. AstroGraph IS a true structural clone (~400
  lines each; relation = the same algorithm with every array doubled
  to [2][objMax] plus transit handling) but is a dedicated-session
  merge with a -L text differential, not a quick pair.
- **F1 — done 2026-08-29** (work log item 85): the three projection
  chain families collapsed onto one static `*ToProj()` set over a
  `PROJ` context in xcharts1.cpp; the eighteen public names stay as
  three-line adapters so no call site changed. Net: 12-bitmap `-Xb`
  differential (-Z/-Z0/-XZ x default/=YXe/=Yf/both), byte-identical,
  every variant proven distinct. The `FSphere*` family is still
  separate (different geometry, the finding's "plus one").
- **E1 — closed as already-satisfied, 2026-08-29** (work log item
  86): a shape audit of all 28 xgeneral.cpp primitives found the
  one-block-per-target order the finding asked for is already the
  codebase's shape — the QT porting pass touched every primitive and
  left them normalized. The apparent stragglers are deliberate:
  DrawFill has no X11 screen branch (X11 offers no flood fill;
  PS/SVG/wire are commented as not implemented), DrawEllipse2 merely
  orders QT before X11, and DrawSz's density is dictated by its
  per-character loop over seven text targets — restructuring it
  risks text rendering for zero behavioral gain. No code change.
- **Next: the T3 harvest** — FOutputSettings() as a loop over
  registry rows under the byte-stable-layout constraint (the
  round-trip script plus a byte-diff of the saved file is the net),
  then generated -H help with its explicit ordering list.

**House habits that keep paying:** falsify every new test before
trusting it; pin renders to literal constants, never to mutable
globals (the midpoint-glyph flake needed two rounds — item 79);
check `git show --stat` file counts after committing; kill orphaned
`astrolog-qt` processes left by interrupted suite runs (startup
diagnostics children); CRLF check is CR count == line count per file.

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

**Done 2026-08-29** (work log item 90) — the contract, each entry
verified by reading or by probe:

- `InitProgram()` (astrolog.cpp) runs once, before any parsing: seeds
  `ciDefa` from compiled defaults, restrictions, the display-name
  pointer tables the parsers read, the custom-object tables, the
  palette. Nothing re-runs it; every later customization layers on
  top and persists for the session — that is the settings model, not
  a leak.
- `FinalizeProgram()` is exit-time deallocation of owned strings and
  arrays; each GUI calls it exactly once at quit.
- `CastChart()` is re-entrant and the GUI recast loop rests on that;
  the one known exception class was relationship-mode state, fixed in
  this fork and pinned by `TestRelationshipModeQt`.
- First-use caches, the class that bites only interactive runs:
  - **Swiss path**: `is.fSwissPathSet` latches on first computation.
    `-Yi` clears it (upstream since 7.00) and `FSwissPlanet()`'s
    static detector clears it when `nSwissEph` changes — but
    re-latching does NOT recover esoteric bodies, because the Swiss
    library caches its orbital-elements state internally on the
    first failed load. Probed live: after a late `-Yi1 "/swe"`,
    `rgszPath[1]` and the latch both update and Cupido still reads
    0. Hence the hard rule: the path must be right before the first
    computation (`-i nrvate.as` at startup).
  - **AstroExpression tries**: built by `FCreateTries()` on first
    parse; contents are the static function names, so the latch is
    harmless.
  - **Star/asteroid enumeration statics** (calc.cpp `lonPrev`,
    `istar`, `ces`, `iast`): per-sweep cursors reset by their own
    callers, not lifecycle state.
- Ordering constraints: `-Yz` before `-n` (recorded in the settings
  writer); `-Yi1` before the first computation (above);
  `InitProgram()` before any parse.

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
incident, the bundled files never nest that way.
**Done 2026-08-29** (work log item 67): all six parsers save and
restore the channel; the live repro showed the failure was worse than
predicted — the abort propagated so far that command-line switches
after the `-i` were skipped too. Regression group `nested-include`,
falsified 3-of-4 failing with the bug reintroduced. The channel itself
still dies properly under T3's parse context.

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
**Done 2026-08-29**, as two legs (a mechanized all-flags flip and a
31-sentinel value fixture), and it caught five shared-core bugs on its
first runs: two buffer overflows (`szLoc[25]`, `szZod[16]` -- one a
crash under fortify), the `-Yu` save oscillation, `:YXp0` multiplying
by 2.54 per metric cycle, and `-YD` never saving standard-object
renames. Work log item 66 has the details. Known limitation: a switch
family the writer has *never* covered is still invisible until T3's
table enumerates the switch surface.

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

**Done 2026-08-29** (work log item 81): extracted as `FSkipEphem()`,
five named sequential ifs, same clause order.

**C4 — The backend selector is three flags and an int with unreachable
corners.** `fEphemFiles`, `fPlacalcPla`, `fMatrixPla`, `fMatrixStar`,
`nSwissEph` 0-3 jointly encode one choice (astrolog.h:1806-1820);
illegal combinations are representable, meaning lives in negations
("ephem files but not Placalc" = Swiss), and each math entry point
dispatches longhand. *Direction:* document the reachable state space
(which switch sequences produce what); collapsing to one derived enum
is worthwhile but belongs to the T3 table work, since the flags are set
by switch parsing. *Cost:* documentation now, enum later.

**Done 2026-08-29** (work log item 89): the state table, the inert
corners, and the -b fall-through toggle trap are documented at the
field declarations (astrolog.h). Better news found while writing it:
the derived reading layer already exists — the `FCm*()` macros in
extern.h are eight named predicates over the five fields, and the
dialogs already collapse the choice to a six-value list. The enum
step would relocate, not create, the meaning; deferred indefinitely.

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

**First pair done 2026-08-29** (work log item 82): the aspect pair is
`ChartAspectCore(flag fRel)` in charts1.cpp. **Second pair done the
same day** (item 83): `ChartMidpointCore(flag fRel)`. **Survey of the
rest, same day** (item 84): Listing and Grid twins are different
layouts, not clones; the Interpret trio differ in actual prose — all
three stay as they are, on purpose. AstroGraph is the one true clone
left (arrays doubled to [2][objMax]); merge it in a dedicated session
with a -L text differential, or not at all.

**D2 — DONE 2026-08-29 (work log item 79). The influence stanzas are
twelve-line clones over table names.**
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

**Closed 2026-08-29** (work log item 86): the normalization already
holds — see the sequencing note. The vtable step remains available if
an export format is ever added, but there is nothing to normalize
first anymore.

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

### Area F — graphics charts (xcharts0-2.cpp), surveyed 2026-08-29

The chart renderers proper. One verdict up front: the wheel machinery
passing parallel `real` arrays (`xsign`, `xhouse`, `xplanet`, `symbol`)
through `DrawWheel`/`DrawSymbolRing`/`FillSymbolRing` is *correct by
this project's own storage rule* (item 63: flat arrays for what the
math sweeps) — not a finding. The net for anything in this area:
graphics captures are only comparable at **pinned chart time** on one
machine (item 4 established renders of "now" differ run to run), so a
refactor here captures fixed-data charts before and after and
byte-diffs those.

**F1 — The projection helpers exist in triplicate, plus one.**
`LocTo/EquTo/EclTo/PriTo/EarTo` × `{Horizon, HorizonSky, Telescope}`
(xcharts1.cpp:608-674, 1023-1096, 1440-1507) are eighteen functions in
three parallel sets, each set differing only in the terminal plot
transform (`PlotHorizon` vs `PlotHorizonSky` vs `PlotTelescope`), with
`FSphere*` (xcharts1.cpp:4178-4222) a fourth family for the sphere
chart. A new projection mode today means transcribing five or six
functions. *Incident:* none recorded — but this is the same
clone-and-vary shape as D1/item 38, one table-name swap from a quiet
bug. *Direction:* one helper set parameterized by the plot transform
(a function pointer or the `CIRC`/`TELE` argument generalized).
*Cost:* low-medium; pinned-capture diff as net.

**Done 2026-08-29** (work log item 85): the chains exist once as
`static *ToProj(..., CONST PROJ *pp, ...)`; the two real per-family
deltas (the sky chart's Loc skips the azimuth flip its plot transform
applies itself; the telescope's Ear mirrors latitude) are named
branches in the core. The 18 public adapters keep every signature.

**F2 — The horizon charts are a clone lineage.** `XChartHorizon` (323
lines) and `XChartHorizonSky` (318) measure ~67% identical;
`XChartTelescope` (662) grew from the same stock. Same treatment as
D1: merge per pair when touched, not as a campaign. *Cost:* medium.

**F3 — `DrawPrint()` has three calling conventions in one signature.**
xcharts0.cpp:70: `sz == NULL && n >= 0` means "set cursor to (m, n)";
`sz == NULL && n < 0` means "advance x by m"; otherwise `m` is a
*color* and `n` selects same-line vs next-line — all against a static
cursor. Every sidebar and info listing threads through it.
*Direction:* split the cursor control into named functions and keep
`DrawPrint(sz, color, fSameLine)` for text; entirely mechanical.
*Cost:* low; touches many call sites but each is a rename.

**Done 2026-08-29** (work log item 87): `DrawPrintTo(x, y)` and
`DrawPrintShift(dx)` own the cursor (now file-scope statics);
`DrawPrint()` only draws. The "many call sites" turned out to be
two — every other caller already used the text convention.

**F4 — `DrawChartX()` is the fifth knower of the mode mapping.**
xcharts0.cpp:2435 switches on `gi.nMode` parallel to `Action()`,
`PrintChart()`, `DetectGraphicsChartMode()`, and the ports' tables.
Counted under A3; the shared table now has five consumers on record.

### Area G — frontends & satellites, surveyed 2026-08-29

express.cpp (the AstroExpression language), atlas.cpp (atlas and
timezone data), and the fork's own qtdriver.cpp/qtdialog.cpp — reviewed
with the same standard as upstream's code. express.cpp earns a largely
clean verdict: `FEvalFunction()`'s 878 lines are an opcode switch over
a data-driven function table with packed type signatures and a
hand-built trie for name lookup — the conventional interpreter shape,
already table-driven where it counts. Its seven backend `#ifdef`s are
platform functions in the VM's standard library (mouse position and
the like), which genuinely are per-platform.

**G1 — Shared core takes a UI handle as a `size_t` that means two
different things.** `DisplayAtlasLookup(..., size_t lDialog, ...)`
(atlas.cpp:1490) casts `lDialog` to `HWND` internally; Windows passes
`(size_t)hdlg` (wdialog.cpp:275), the Qt port passes literal `1`
(qtdialog.cpp:1912) — the same parameter is a window handle on one
backend and a boolean on the other, and the core branches on both
interpretations. *Incident:* none yet; it worked because the casts are
guarded, which is luck of the `#ifdef` layout. *Direction:* split the
parameter into what it actually is — a "results go to the open dialog"
flag plus a per-backend sink the ports own; small, self-contained.
*Cost:* low. Tagged T6 (backend knowledge above the device layer).

**G2 — The atlas loaders are the payload half of B3.** `FLoadAtlas`/
`FLoadZoneRules`/etc. (atlas.cpp:906-1392) read their data through the
`is.fileIn` channel and hand-roll their own line loops — the same
family as B1's readers. Any B1/B3 fix must count these four as
call sites. *Cost:* counted under B1/B3.

**G3 — The Qt port joins everything by English label strings.** Menu
actions, context menus, and hotkeys bind by menu-item label text —
a deliberate design (labels come generated from `astrolog.rc`, and 42
context menus + 264 shortcuts + 258 parity checks assert every label
resolves). The invariant that makes this safe — *a menu label is an
identifier; changing one is an interface change* — lives only in the
suite's failure mode today. *Direction:* state the invariant in T8's
conventions doc; the design itself stands. *Cost:* documentation.

**G4 — The port grew its own file-scope global layer.** 29 `s_*`
statics in qtdriver.cpp (shared chart-mode group, tracking arrays, the
text window...). *Incident:* gotcha 7 — code assuming
`s_rgpaChartMode[0]` was "Standard Radix" broke when menu build order
changed; fixed by lookup-by-value, but the arrays still invite index
assumptions. *Direction:* gather them into one `QTUI` struct (the
port's `wi` equivalent) so ownership is visible and the "look up by
value, never by index" rule (gotcha 7) sits beside the data it
protects. *Cost:* low, mechanical, port-only — no oracle needed.

### Area H — data model & headers, surveyed 2026-08-29

astrolog.h (constants, types, the four structs, feature macros),
extern.h (the 636-declaration manifest plus alias macros), data.cpp
(the tables). Credit first: **the object taxonomy is good design** —
chained range constants (`custLo = uranLo`, `oNorm = cobHi`,
astrolog.h:658-683) with predicate macros (`FItem`/`FNorm`/`FMoons`/
`FCob`/`FCust`/`FStar`/`FThing`) that compose, so inserting a category
shifts every range consistently. Its only documentation is the
predicates themselves — that goes in T8's conventions doc. The
`OBJSET`/`rgobjset` named-row pattern (item 63) is the established
template for tables that deserve it.

**H1 — The restriction defaults are the surviving instances of the
rObjOrb failure class.** `ignore[]` and `ignore2[]` (data.cpp:198, 210)
are `objMax`-sized anonymous value runs: a miscount misaligns every
later object silently, exactly the defect that sat in `rObjOrb[]` for
years, and zero-fill hides a short list. *Incident:* item 63's found
bug is this class; these are its remaining members. *Direction:* a
static `tools/defaults_audit.py` machine-diffing data.cpp's initializer
runs against upstream astrolog.as's own restatement of the defaults —
the exact method that caught rObjOrb, made standing alongside the four
rc audits. Named-row conversion only for tables that earn it.
**Done 2026-08-29** — and the audit found `ruler2[]` one value short
(83 in 84 slots, upstream's bug too, benign only because everything
after the gap is zero) on its very first run. Also surfaced that
astrolog.as's transit-restriction rows 52-133 still carried the stale
`-YR`-for-`-YRT` output of the settings writer fixed earlier; the file
is corrected. Six falsification classes verified caught.

**H2 — The feature macros are bare colliding words that are always
on.** `TIME`, `PS`, `META`, `SWISS`, `GRAPH`... (astrolog.h:82-173)
are effectively permanent (gotcha 8) yet force include-order
contortions (Qt headers must precede astrolog.h because its bare words
collide with Qt internals) and keep dead `#ifdef`-else paths alive
(`MdyToJulian`'s unreachable `return 0`). *Direction:* minimum is the
T8 rule ("new macros take a prefix; these three collide"); the fuller
move — prefixing `META`/`PS`/`TIME` — is one mechanical sweep each,
high upstream-divergence, worth it only for the three that collide.
*Cost:* low each; decide per macro.

**H3 — One header pair is the whole module system.** Every compilation
unit includes astrolog.h + extern.h; the "From x.cpp" comment sections
in extern.h are the only module boundaries that exist, and any edit to
the `US` struct rebuilds the world (which commit 01f52b1 made honest —
previously it rebuilt *nothing*, which was worse). *Verdict:* accept
for this codebase's size; per-module headers are a real modularization
project with real merge cost, and nothing above depends on it. Recorded
so nobody mistakes the omission for an oversight.

---

## Sequencing — the first increments

The survey pass is complete (all eight areas, 2026-08-29). The findings
above sort into a natural order — smallest risk and best nets first,
each independently shippable:

1. ~~**H1** — `tools/defaults_audit.py`~~ — **done 2026-08-29**; found
   `ruler2[]` one short on its first run. See H1.
2. ~~**B5** — the full-coverage settings fixture~~ — **done
   2026-08-29**; caught five shared-core bugs including two buffer
   overflows. See B5 and work log item 66.
3. ~~**B3** — save/restore `is.fileIn` around nested includes~~ —
   **done 2026-08-29** (work log item 67); the interim fix until T3's
   parse context retires the channel entirely.
4. **T3's switch registry** — promoted at the maintainer's direction
   (2026-08-29), and its migration phase is **complete**: M1-M10 moved
   every spelling into the registry and dissolved all four parsers in
   one day. Next in T3: the harvest — FOutputSettings() as a loop over
   the rows (T4), generated -H help diffed against the hand-written
   text, and the audits reading the registry instead of regexing C. **M1 and M2 done 2026-08-29** — registry, driver, 39
   switches across the -YA*, -Yj*, -YJ*, -YR*, -Y7* and -Yk* families;
   all differentials byte-identical. M3 added the parse context and
   retired is.fileIn; M4-M6 deleted NProcessSwitchesRare(),
   NProcessSwitchesRareX(), and NProcessSwitchesX(). Only the main
   FProcessSwitches() cases remain -- chart types, chart info, the -R
   restrictions, -W passthroughs, and the imperative switches. M7
   onward takes them a letter family at a time.
5. ~~**C3, E1, D1/F1/F2** — the remaining quality increments~~ — all
   resolved 2026-08-29 evening: C3 (FSkipEphem), D1 (two merges + the
   survey verdicts on the rest), D2 (RULERSYS), E1 (closed as already
   satisfied), F1 (one projection chain), F3 (DrawPrint verbs). The
   documentation themes landed the same evening: T8 (CONVENTIONS.md),
   A4 (the lifecycle contract, with a probe-verified correction to
   QT_TESTING.md), C4 (the backend state table), T1 moves (1) and (2)
   (the classification on the structs; the Borrow guard).

**The specified queue is empty as of 2026-08-29** — and the phase-1
retrospective (the registry review, same evening) named the rough
edges phase 1 accepted. Phase 2 below turns those into increments.

---

## Phase 2 — the polish plan (drafted 2026-08-29, late)

Phase 1's verdict on the registry: better on fragility, auditability
and bug yield; a wash on volume; the soup contained in labeled jars
rather than gone. Phase 2 is the hard cleanup those jars deserve. Same
rules as phase 1: every increment independently shippable, every one
proven by a net, docs travel in the increment's commit, and anything
that measures as no-gain gets closed with the measurement instead of
done anyway.

**Tranche 1 — finish what the registry started** (the retrospective's
own concessions, in dependency order):

- **P1 — done 2026-08-29** (work log item 96): `switch.cpp` (4,453
  lines, LF per fork convention) holds the registry, handlers,
  dispatch, and `FProcessSwitches`; astrolog.cpp is the 896-line
  program shell again. One extern surfaced (`AdjustRulership`, used
  by handlers, never declared); five makefiles gained the object.
  Matrix byte-identical over 14,378 lines — after a first run
  tripped the harness's own short-path rule (deep-path baseline
  emits the Swiss truncation warning; the header says so).
- **P2 — done 2026-08-30** (work log item 97): PARSEIN carries
  argc/argv/fOr/fAnd/fNot/pctx, built once per switch by
  FProcessSwitches(); 187 handler signatures became
  `(szSwitch, PARSEIN *pin)`, and the passthrough parsers in
  wdriver.cpp/qtdriver.cpp take `(pos, PARSEIN *)` too. The
  FSwitchF()/FSwitchF2() macros now read the prefix flags through
  `pin` — the scope-capture that used to force the threading is the
  documented contract instead. One handler (NSwR) keeps local
  walking copies of argc/argv on purpose: it consumes arguments in a
  loop and returns argcIn - argc. Matrix byte-identical.
- **P3 — done 2026-08-30** (work log item 98): SWITCHDEF rows carry
  `carg`; the dispatch makes the identical FErrorArgc call with the
  row's own spelling as the label. 41 handlers converted under the
  strict rule (check literally first, label equals row spelling,
  handler serves exactly one exact row); the near-misses — rows like
  Yj0 that error under the family label Yj, prefix rows, per-suffix
  arity — keep their checks on purpose. Net: a 41-spelling
  missing-argument stderr byte-diff plus the matrix, both identical.
- **P4. De-soup by measure.** Per prefix row, count the spellings its
  handler personally parses. Where the suffix set is closed and each
  suffix is an independent flag/value (the -b0/-b1/-b2 kind), promote
  them to exact rows and shrink the handler; where suffixes share
  fall-through semantics (the -b backend/fEphemFiles toggle), keep
  the handler and say why in a comment. Deliverable includes the
  before/after prefix-row count. Strictness policy from phase 1
  applies unchanged. Net: matrix per family. *Cost: medium, many
  small commits.*

**Tranche 2 — the deferred heavy items:**

- **P5. The AstroGraph merge**, exactly as specified in the D1
  verdict: a dedicated block at session start, nothing else in
  flight, -L text differential across single/relation/transit modes
  as the gate, abort-and-record if it fights back. Measured 76%
  line-identical with the differences interleaved through an [i2]
  chart dimension — the equivalence argument between planet[] and
  rgpcp[] data paths is the real work, not the editing.
- **P6. Borrow conversion becomes a paced campaign.** Phase 1 made
  *Sav conversion opportunistic; phase 2 promotes it: family-by-family
  through xcharts1.cpp's ~60 sites (the worst file), a handful of
  sites per commit, each commit gated by the -Xb pinned-date bitmap
  differential over the charts those sites draw. Stop the campaign
  the first time a conversion is not obviously mechanical (a
  conditional restore, an ordering dependency) and record why.
- **P7. Prefix the three colliding feature macros.** H2's fuller
  move: `PS`, `META`, `TIME` are why Qt headers must precede
  astrolog.h. One mechanical sweep per macro (build-breaks-find-all),
  then delete the include-order rule from CONVENTIONS.md rather than
  documenting around it. Net: all four builds + suite; no output can
  change. *Cost: low each, high churn-width; do one and reassess.*

**Tranche 3 — survey first, then decide** (open findings that need
reading before they deserve increments; each gets a measured verdict
the way D1's tail and the harvest did):

- **P8. D3/D4** — the print pipeline's modal global state and
  PrintChart's mode zoo. Survey what a table or context struct would
  actually displace; the text-diff tooling is the ready-made net.
- **P9. G4** — the Qt port's 29 `s_*` file-scope globals: classify
  like T1 (wiring vs cache vs scratch), fold the trivial ones.
- **P10. C1/C5/C6, G1/G2** — calc.cpp's three-modules-in-a-file, the
  Swiss index mapping, the chart-position slot aliases, the size_t
  UI handle, the atlas loader channels. Order by incident risk when
  surveyed; C5's mapping table is first candidate (it has phase-1
  incident history).

Sequencing: P1 → P2 → P3 (each enables the next's mechanical
confidence), then P4 interleaved with tranche 2 at will. P5 wants a
fresh session. Nothing in phase 2 is urgent; everything in it is the
difference between "contained" and "clean".

---

## Done

- **T2 partial** — rulership cross-table invariant asserted
  (qttest.cpp `rulership` group), commit `b36415b`, 2026-08-29.
- **T2/T4 partial** — per-object settings struct `rgobjset[]` with named
  rows; settings round-trip script. Commit `caf3205` (work log item 63).
- **T5 partial** — definition parse/format unified, name lookups folded
  (items 59-62), commits `9218317`..`9c83003`.
- **The whole of 2026-08-29** — the audits (`defaults_audit`,
  `registry_audit`), the three-leg round trip, the nested-include and
  parse-context work, the T3 migration M1-M10, the registry hardening,
  and D2: work log items 65-81 carry each with its commit. T3's
  migration phase is closed; T4's drift class is closed by audit; T6's
  worst evidence (missing-branch parsers) is gone with the parsers.
- **Bugs fixed along the way, all shared-core**: ruler2[] one short;
  astrolog.as's stale -YR/-YRT rows; two buffer overflows (szLoc,
  szZod) plus `sprintf2` unbounded on non-Windows; -Yu never reaching
  a fixed point; :YXp0 metric double-conversion; -YD losing
  standard-object renames; is.fileIn clobbering; -YXW's missing arity
  check; -YYI dead since upstream wrote it. Every one found by a net
  built the same day.
