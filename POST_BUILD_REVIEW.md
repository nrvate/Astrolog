# Post-build review

A file-by-file review of the source, started 2026-09-12, after the port
and its CI were built. **This document records; it does not fix.** The
maintainer's instruction was to write everything down in detail and
decide on fixes separately, so every finding here is open unless its
status line says otherwise, and each carries the evidence that would
let someone act on it without re-deriving it -- the exact command, the
exact output, the build it was measured on.

The order follows the program in from its entry point: `astrolog.cpp`
first, then whatever `main()` reaches. Each file gets a section with
three parts: what was checked and found fine (so the next pass does not
re-check it), the findings, and cross-references handed to the review of
another file. Findings are numbered by file letter, `A-1` for the first
finding in `astrolog.cpp`, and the number never changes once given, so a
later note can cite it.

Severity is about consequence, not about how hard the trigger is:

- **high** -- wrong numbers, lost user data, a crash on a documented path.
- **medium** -- undefined behaviour or a crash on a reachable but unusual
  path; a documented switch that misbehaves.
- **low** -- a wart with a bounded, visible consequence.
- **note** -- upstream behaviour worth knowing about, or a documentation
  drift; not proposed for change unless it is cheap and safe.

"Upstream" means the line is identical in CruiserOne's 8.00 (`5bf172e`)
and the port did not introduce it. That is not a reason to leave it: the
Windows build compiled from this tree is the same core, so an upstream
bug here is a bug in the shipped product on every platform. It is a
reason to check the fix against `Makefile.win` as well.

Every finding was measured with the binaries in the tree on
2026-09-12 (`./astrolog` built 18:52, plain `-O` console build), plus a
console build with `-fsanitize=address -g -O0 -DQTTEST` made for this
review into `obj-asan-review/` with the binary at
`/tmp/astrolog-asan-review` -- the same recipe `tools/asan-sweep.sh`
uses, without its `make clean-console`, so nothing in the tree was
disturbed. Where a finding says "test build" it means that binary; the
range guards (`AssertIndex`) are live only there.

---

## A. `astrolog.cpp` -- reviewed 2026-09-12

944 lines: `InitColors()`, `AdjustRulership()`, `Action()`,
`InitVariables()`, `FProcessCommandLine()`, `NParseCommandLine()`,
`NPromptSwitches()`, `InitRestrictions()`, `InitProgram()`,
`FinalizeProgram()`, `main()`. Upstream's copy is 3,460 lines; the
difference is the two switch parsers, which moved to `switch.cpp` (see
REFACTORING.md, "The registry as built"). The fork's own changes to this
file, from a line-ending-normalised diff against `5bf172e`, are: the
checked-table types in `InitColors()`/`AdjustRulership()`, `sprintf2`
everywhere, the `cb > cchSzLine` refusal in `FProcessCommandLine()`, the
`ciSave` assignment guarded off the GUI builds, `FinalizeProgram()`
calling `FinalizeQt()`, and the two `#ifdef QT` blocks in `main()`.
REFACTORING.md's Area A survey (A1-A5) covered the structure; this pass
is about behaviour, and nothing below repeats an A1-A5 item.

**The Qt build enters through this file's `main()`.** There is no
`main()` in `qtdriver.cpp`; the `QApplication` is created lazily at
`qtdriver.cpp:5944` when a chart first needs a window, the way X11's
`InteractX()` works. So everything in `main()` -- the `-Q` loop, the
tty prompt, `Terminate()` -- is live in the Qt build, which matters for
A-8.

### Checked and found fine

- **`NParseCommandLine()` bounds.** `argc` is capped at
  `MAXSWITCHES-1` with a warning, so `argv[argc] = NULL` lands at index
  at most `MAXSWITCHES-1` of a `MAXSWITCHES` array. A quote is only
  closed when the character after it is below `'@'`, which is upstream's
  way of letting `"it's"` through; unmatched quotes run to end of line.
- **`FProcessCommandLine()` refusal** of a line over `cchSzLine` (work
  log item 174) is in front of the copy, and the "-i" rewrite of a bare
  filename goes through a bounded `sprintf2`. A bare word that is a
  *directory* is accepted by `fopen()` on Linux (it is not on Windows),
  becomes `-i "ephem"`, and then fails cleanly: measured
  `-M0 1 "ephem" -M 1` prints `Failed to parse command line: ephem` and
  casts the chart anyway. Not a finding.
- **`InitVariables()`'s `ClearB` range** (`fListing` to `fLoop`) is
  byte-identical to upstream's struct layout -- no fork-added field sits
  inside it. What it clears is A-4.
- **`FinalizeProgram()`** frees every `char *` member of `US`, `IS` and
  `GS` listed in `astrolog.h`, all 58 `szExp*` included; the exit-time
  `is.cAlloc != 0` warning stayed silent on runs with `-n`, with `-kh
  -os`, with `-Q`, and with a named chart (`-zi "Some Name" "Some
  Place"`). The upstream typo it fixes (`szLifeArea[i] != szLifeArea[i]`,
  which freed nothing) is real and correct.
- **`InitRestrictions(fTrue)` in `main()`** matches `wdriver.cpp:711`
  in position, with one difference recorded as A-11.
- **The Qt startup order** -- `InitProgram()`, `astrolog.as`,
  `us.fGraphics = fTrue`, `FInputData("now")`, the ring copies, then the
  real command line -- means a chart file or `-qb` on the command line
  overrides "now", and `_X` on the command line still turns graphics
  off. That is the documented intent and it holds.
- **`InitColors()`'s moon rule** reads `kObjA[]` of the orbited body,
  which for every moon, COB and custom-object case is a lower index and
  therefore already computed in the same loop. The one index it can
  produce that is *not* lower is `-1`; that is A-2.
- **`AdjustRulership()`** is upstream's logic under checked tags;
  `sig > 0` is the guard and sign tables use `-1` for none, so the
  `rgRules2 >= 0` test is the right domain (CONVENTIONS.md, index
  domains).

### Findings

**A-1 -- `Action()` closes the `-os` file and leaves `is.S` pointing
into freed memory.** *medium, upstream, every build.*

`Action()` ends with

    if (is.S != stdout)
      fclose(is.S);

and nothing resets `is.S`. The next thing the console `-Q` loop does is
`PrintL2()` (astrolog.cpp:934), which is `PrintSz("\n\n")`, which
`putc()`s into `is.S`. Measured on the test build under gdb, stopped at
that line after `-Q -n -os q.txt` had cast one chart:

    $1 = (FILE *) 0x515000000f80          is.S
    $2 = (FILE *) 0x7ffff6e1b780          stdout
    is.S->_fileno = -1
    __asan_describe_address(is.S):
      0x515000000f80 is located 0 bytes inside of 472-byte region
      freed by thread T0 here:
        #1 _IO_deallocate_file  libio/libioP.h:862
        #2 _IO_new_fclose       libio/iofclose.c:74
      previously allocated by thread T0 here:
        #1 __fopen_internal     libio/iofopen.c:65

So it is a use-after-free of a `FILE`, on a documented switch. It has
never crashed because glibc's `fclose()` marks the object `_IO_NO_WRITES`
before freeing it and `putc()` reads that flag back out of the freed
block -- which is exactly the state that breaks the moment the allocator
reuses the block, and the first 16 bytes of a freed chunk are overwritten
by the tcache list pointers on the very next `free()`. That is work log
item 165's mechanism (a `FILE *` nothing had opened), and item 165 took
an hour to bisect because the symptom was nondeterministic.

**ASan cannot see this one.** The access happens inside libc, which is
not instrumented; ASan's `fprintf` interceptor checks the format
arguments, not the stream. `tools/asan-sweep.sh` ran the switch matrix
over `-os` many times and reported nothing. The only reason it is known
is `__asan_describe_address()` on the pointer by hand. Add this to the
list beside the fortify note in CLAUDE.md: "a freed `FILE *` is a third
class ASan structurally cannot see".

Who is exposed:

- The console builds, on every `-Q`/`-Q0`/`-0q` iteration with `-os`.
- Windows: `wdriver.cpp:2936` decides whether to draw a text chart with
  `is.S == stdout`; after an `-os` `Action()` it is neither `stdout` nor
  open. Not measured under Wine; handed to the `wdriver.cpp` review.
- Qt: `CaptureTextToFileQt()` (qtdriver.cpp:1194-1217) saves and
  restores `is.S` around its own `Action()` and says why. But `-os` is a
  settings switch (`switch.cpp:3434`) reachable from `astrolog.as`, a
  macro and the Enter Command Line box, and then *every* text redraw
  goes through `Action()` and leaves `is.S` dangling until the next one.
  Whether anything prints between two redraws is a `qtdriver.cpp`
  question -- handed there as Q-open-1.

*Fix shape:* one line, `is.S = stdout;` after the `fclose()`.
*Net:* a suite probe that sets `is.szFileScreen`, runs `Action()`, and
asserts `is.S == stdout` -- fails today, passes with the line, and does
not depend on the allocator.

**A-2 -- `InitColors()` indexes `kObjA[-1]` when a body that orbits
nothing is given the "Planet" colour.** *medium, upstream, both
builds through the switch; the dialogs refuse it.*

**Status: fixed** — `InitColors()` now computes the orbit index first
and falls back to `kLtGray` (the same fallback the element branch uses
for an unruled body) when `ObjOrbit(i)` returns -1, instead of feeding
the -1 to `kObjA[]`. Verified with the review's lines on a
`-DQTTEST`-guarded ASan build: `-YkO 1 1 Planet -n` and
`-YkO Asc Asc 29 -n` both aborted with
`AssertIndex: index -1 outside 0..133` before, both exit 0 after, and
`-YkO 1 1 29 -n -k` renders with the Sun in the fallback gray instead
of reading the int before `kObjA[]`. Chart and switch matrices
byte-identical to the pre-fix tree (the packed sub-option colour rows
the matrices do exercise never route a non-orbiting body here, so the
default path is untouched).

    k = kObjA[!FBetween(i, cobLo, cobHi) ? ObjOrbit(i) : oJup + (i - cobLo)];

`ObjOrbit()` (general.cpp:1046) returns `-1` for the Sun, the cusps, the
Uranians, the dwarfs, the stars -- everything that is not a planet, a
moon, a node or a custom object. `kPlanet` (29) is meant for the moons
and the `-YkO` registry row (switch.cpp:203) allows `0..kMax-1` = 29 for
objects `0..starLo`:

    /tmp/astrolog-asan-review -Yi1 ephem -YkO 1 1 Planet -n
    AssertIndex: astrolog.h:1813: index -1 outside 0..133
    exit 134

    /tmp/astrolog-asan-review -Yi1 ephem -YkO Asc Asc 29 -n
    AssertIndex: astrolog.h:1813: index -1 outside 0..133

    ./astrolog -Yi1 ephem -YkO 1 1 29 -n -k      (plain build)
    renders; reads the int before kObjA[] as the Sun's colour

The two Object Settings dialogs validate with `FValidColor2()`, whose
ceiling is `cColor2-1 + 2` = 27, so neither GUI can produce it by hand;
the Moons dialog admits `kPlanet` only for `moonsLo..cobHi`, where
`ObjOrbit()` is always defined. So the exposure is the switch, a
settings file, an AstroExpression, and a hand-edited `astrolog.as`. Note
the `GRDOBJI` guard did its job: this is the first `-1` subscript found
by the range guard rather than by a crash.

*Fix shape:* either `InitColors()` treats `ObjOrbit(i) < 0` as
`kLtGray` (the same fallback the element branch uses for an unruled
body), or `-YkO`'s row validates per object. The first is three lines
and covers every route in.
*Net:* the `-YkO 1 1 Planet` line under the test build, which aborts
today.

**A-3 -- `-0q` with stdin at end-of-file is an unbounded loop that
writes to disk as fast as it can.** *medium, upstream, console builds.*

    ./astrolog -Yi1 ephem -0q -n -os q3.txt < /dev/null > q3.out
    ... killed after about two minutes:
    q3.out: 9,082,388,443 bytes, every line
    "Value 0 out of range from 1 to 12."

The cycle, traced in the source: `main()` reaches `us.fNoQuit`, loops to
`LBegin`; `NPromptSwitches()` calls `InputString()`, `fgets()` returns
NULL, `Terminate(tcForce)` **returns** because of `-0q` (its first
statement), the fixed EOF branch (item 176) hands back an empty line;
`FProcessSwitches()` of nothing succeeds; `Action()` finds
`is.fHaveInfo` cleared by `InitVariables()` and calls `FInputData("tty")`,
whose month prompt gets EOF the same way, parses `0`, prints the range
error and fails; `Action()` returns early -- and `main()` goes round
again. Nothing in the cycle blocks and nothing in it can exit.

Commit `912175e` (item 176) noticed this in passing -- "an ASan build
times out because -0q at EOF also spins" -- while fixing the read next
to it, and left it. The size of the output is the new fact: a script
that runs `astrolog -0q ... </dev/null` fills the disk. `-0q` is a
documented switch that `tools/switch-matrix.sh` exercises (with stdin
attached, which is why the matrix never saw it).

Two side effects ride along:

- With `-os`, each iteration's `Action()` calls `fopen()` and the early
  `return` skips the `fclose()`, so file descriptors leak one per cycle
  until `fopen()` fails with EMFILE and the run falls back to stdout.
  That is A-9's early return doing damage.
- The HTML trailer and the `-o` write are skipped on the same path.

The GUI builds are not exposed: their `-0q` refuses the window's close
(`wdriver.cpp:1130`, `qtdriver.cpp:332`), so `Action()` never returns
there.

*Fix shape:* when `InputString()` hits EOF under `-0q`, the process has
no operator left; `main()`'s loop should treat `feof(stdin)` as the end
regardless of `fNoQuit`, or `Terminate()` should exit on `tcForce` even
under `-0q` when stdin is exhausted. Either is a few lines; the second
also retires the guard-loop concern in item 176.
*Net:* the command above under `timeout 5`, asserting exit before the
timeout and an output under a kilobyte.

**A-4 -- One `-Q` iteration silently drops the five on-by-default
sub-options.** *low, upstream, console builds.*

**Status: fixed** — `InitVariables()` now clears the chart-type block
and the table-chart-type block as two `ClearB()` ranges and leaves the
"Chart suboptions" block between them, so `fGridConfig`, `fAspSummary`,
`fMidSummary`, `fInfluenceSign` and `fLatitudeCross` survive the loop
reset. Verified with the review's recipe (same `-g` cast once directly
and once re-entered through one `-Q` prompt): before, the second cast
was missing the `Stellium-3 with Moo: 14Lib19 ...` line; after, the
re-entered cast is byte-identical to the direct cast, and the same
holds for `-j` with its `fInfluenceSign` sign table. Chart and switch
matrices byte-identical (the matrices never drive a second `-Q`
iteration, which is why nothing there caught this).

`InitVariables()` clears `fListing` through the byte before `fLoop`,
which is the chart types, the table types, **and the whole "Chart
suboptions" block** -- including `fGridConfig` (`-g0`), `fAspSummary`
(`-a0`), `fMidSummary` (`-m0`), `fInfluenceSign` (`-j0`) and
`fLatitudeCross` (`-L0`), all of which `astrolog.h` marks "on by
default". Measured: the same `-g` grid cast once, and cast again after
one `-Q` prompt with the chart re-entered, differ in exactly one line --
the second has no `Stellium-3 with Moo: 14Lib19 ...` summary, because
`fAspSummary` is gone.

    ./astrolog -qb 7 4 1976 12 0 8 122:19:55W 47:36:22N -g         > g1
    printf -- '-g -qb 7 4 1976 ... -n\n.\n' | ./astrolog -Q -qb ... -g
    diff: 55c55  < Stellium-3  with Moo: 14Lib19 and Plu:  8Lib59 ...

The user sees fewer lines and no message. The comment above the function
says "reset things like which charts to display, but leave setups such
as object restrictions and orbs alone"; the sub-options are setups.

*Fix shape:* clear the chart-type block and the table block as two
ranges and leave the sub-options, or re-assert the five defaults after
the clear. The struct order makes the two-range form a two-line change.
*Net:* the diff above, empty.

**A-5 -- The console prompt reads 255 characters where every other
command-line path takes 1,020, and the excess becomes the next command
line.** *low, upstream, console and X11.*

**Status: fixed** — `main()`'s `szCommandLine` and `CommandLineX()`'s
are `cchSzLine` now, like `FProcessCommandLine()`, the settings-file
reader and the Qt command box. Verified with the review's probe: a
260-character line at the `-Q` prompt plus a `.` line used to produce
three `Input command line >` prompts and `Unknown subswitch '-00'`
twice (once per fragment); after, one prompt per input line and one
error for the over-long command. Chart and switch matrices
byte-identical (no matrix invocation comes near 255 characters, which
is why the truncation never showed there).

`main()` declares `char szCommandLine[cchSzMax]` and `NPromptSwitches()`
reads it through `InputString()`, which is `fgets()`. `fgets()` leaves
what does not fit in stdin, so the next prompt reads the tail of the
previous line as a fresh command. Measured with a 260-character line at
the `-Q` prompt: three `Input command line >` prompts for two lines of
input, and `Unknown subswitch '-00'` twice, once per fragment.
`FProcessCommandLine()`, the settings-file reader and the Qt command box
all take `cchSzLine`; `CommandLineX()` (xscreen.cpp:530) has the same
255-byte buffer as `main()`. Item 174 refused an over-long line here
precisely because "half a command line is a different command line";
this is the same hazard one prompt up.

*Fix shape:* `cchSzLine` in both declarations, 1,020 bytes of stack
each.
*Net:* the 260-character line, asserting one prompt per input line.

**A-6 -- The UTF-8 byte-order mark is written after the HTML header.**
*low, upstream, every build.*

    ./astrolog -n -kh -Yao3 -os b.htm
    offset 96:  " >  357 273 277  A s t r o l o g ..."

The BOM lands after `<html>...<body><font face="Courier">`, because
`Action()` prints the HTML head first and writes the mark at
"If the -kh switch..."'s end (astrolog.cpp:187) afterwards. A BOM that
is not the first three bytes is a U+FEFF zero-width no-break space in
the body, invisible in a browser, and the file is not a BOM-marked file
to anything that checks. The `<meta charset>` in the head is what makes
browsers read it correctly, so nothing is visibly broken; it is simply
in the wrong place. Windows writes the same file.

*Fix shape:* move the three-byte write above the `fHTML` block.
*Net:* `head -c 3 b.htm` equals `EF BB BF`.

**Status: fixed** — the BOM's three bytes are written before the HTML
head in `Action()`, so `-kh` output begins with `EF BB BF` and the
mark is what it claims to be. Verified with the repro above
(`head -c 3` equals `EF BB BF`; previously the mark sat at offset 96)
and byte-identical matrices apart from the three moved bytes at the
head of `-kh` legs.

**A-7 -- A comment in this file describes the Qt command box's old
limit.** *note, this fork.*

astrolog.cpp:397-399 says "Windows caps its Enter Command Line box at
cchSzLine and the Qt one at cchSzMax". The Qt one has been `cchSzLine`
since `FRunCommandLineQt()` (qtdialog.cpp:3717-3724) was rewritten, and
that function's own comment records why. Two lines to bring this one in
line.

**Status: fixed** — the comment now says both GUIs cap the box at
`cchSzLine`, which is what the code has done since
`FRunCommandLineQt()` was rewritten; doc drift caught up with the code.

**A-8 -- `-Q` and `-Q0` in the Qt build fall through to a stdin
prompt.** *note / knowing divergence to record, this fork; by reading,
not run.*

Windows' `WinMain` never reads `us.fLoop`. The Qt build runs this
file's `main()`, so with `-Q` in `astrolog.as` (which a console user
might well have), closing the chart window drops the process into
`NPromptSwitches()` on stdin: from a terminal it waits for input nobody
expects to give; from a desktop launcher stdin is `/dev/null`, EOF ends
it, and the user sees nothing wrong. With `-Q0` the prompt comes
*before* the window, on every launch. The Qt build is also a console
program under `_X` (the PrintSz pager comment explains the arrangement),
and there the loop is right, so the fix is not to remove it -- it is to
skip it when a window was shown, or to record the divergence. Not
measured: a headless run cannot close the window.

**Status: fixed** — `main()` now skips the loop-back (clearing both
`us.fLoop` and `us.fNoQuit`) when the Qt build has shown a window,
tested as `gi.qapp != NULL`: the QApplication is created lazily at the
first window, so NULL means the process has run as a console program
all along and keeps the loop. This matches Windows' `WinMain`, which
never reads `us.fLoop`, so nothing is added to the divergence list.
Measured beyond the review: with `-Q` and stdin held open the
pre-fix binary never exits at all (the review's terminal case), and
the headless probes still cannot reach the loop-back (Action() stays
in the event loop until the window closes), so the windowed path is
validated by reading; the console path is pinned empirically -- with
`_Xt` and stdin held open the binary still waits at the prompt after
the change, exactly as before it.

**A-9 -- `Action()`'s early return skips the file close, the HTML
trailer and the `-o` write.** *note, upstream, console builds.*

**Status: closed** — unreachable, no code change. `FInputData("tty")`
cannot return fFalse: its tty branch ends in `return fTrue` and every
prompt on the way there (NInputRange/RInputRange) loops until it gets a
value in range, so the only ways out are success or never returning.
Before the A-3 fix, EOF inside those loops either exited the process
(no `-0q`) or span forever (`-0q`) — the review's fd-leak-per-cycle
corollary rode on the main-loop trace that A-3's gdb evidence corrected;
in the real cycle `Action()` is entered at most once after the input is
gone, and after A-3's fix the EOF case exits inside `Terminate()`. The
`return` therefore cannot fire in any build, today or before.

    if (!is.fHaveInfo && !FInputData(szTtyCore))
      return;

The path is only reachable when the tty prompt fails, which is the
`-0q`-at-EOF case (A-3) and Control-D at the month prompt. The
consequence is the descriptor leak measured under A-3. A `goto LDone`
instead of the `return` would fall through to the existing cleanup; it
is listed separately because A-3's fix may make it unreachable.

**A-10 -- The HTML trailer closes two fonts.** *note, upstream.*

`PrintSz("</font>\n</font>")` against one `<font face="Courier">`.
`AnsiColor()` (general.cpp:1692-1707) opens a colour `<font>` per colour
change and closes the previous one, so at the end of a coloured chart
one colour font is still open and the second `</font>` is for it; with
colour off the second is a stray close tag, which every browser
ignores. Correct by accident; not worth changing.

**Status: closed** — changing nothing, by the review's own bound: in a
coloured chart the second `</font>` closes the colour font `AnsiColor()`
leaves open, which is exactly right, and in a colourless chart the
stray tag is ignored by every browser. A change here would be churn
against behaviour that is either correct or invisible.

**A-11 -- `InitRestrictions(fTrue)` runs only if the command line
parsed.** *low, this fork, Qt only.*

**Status: fixed** — `main()` now captures `FProcessSwitches()`'s
result and calls `InitRestrictions(fTrue)` unconditionally after it,
before branching on the result, mirroring wdriver.cpp:711 where the
store follows `FProcessCommandLine()` whether or not the command line
parsed. (Above the *call* would be wrong: the store must follow the
command line's own `-R`/`-RY` restrictions the way Windows' store
follows its command line; it is the `if` around `Action()` that it
leaves.) Verified in the Qt build under gdb at `Terminate()` with
`-R Sun -#` (one bad switch): before, `ignore[oSun]` was 1 while
`ignoreMem[oSun]` was 0 — the "Recall" set still the compiled defaults
InitProgram() stored; after, both are 1. The suite's good-case probe
(`ASTROLOG_QT_RECALL_PROBE=1`, `-R Sun`, no bad switch) still passes,
and the console build is untouched by the guard.

Windows calls it unconditionally after `FProcessCommandLine()`
(wdriver.cpp:711). The Qt block is inside `if (FProcessSwitches(...))`,
so a command line with one bad switch leaves the "Recall" set at the
compiled defaults -- the exact state item 5720139 fixed for the good
case. The settings file has already been read by then, so what is lost
is the user's `astrolog.as` restrictions, not the command line's. Moving
the call above the `if` matches Windows.

### Handed to other files

- **Q-open-1** (`qtdriver.cpp`) — **answered: not exposed.** RedrawQt
  brackets its own `Action()` with an `is.S` save/restore, so nothing
  prints through the dangling handle in the GUI; see the QD section.
- **W-open-1** (`wdriver.cpp`): `wdriver.cpp:2936` tests
  `is.S == stdout` to decide whether to draw text; after an `-os`
  `Action()` it is a dangling non-stdout pointer. What does the Windows
  text path do on the next redraw? **Moot after A-1's fix:** `is.S` is
  `stdout` again by the time anything reads it.
- **S-open-1** (`switch.cpp`): the `-YkO` row's colour range is
  `0..kMax-1` for every object it covers; A-2 is the consequence. The
  Moons dialog's `FValidColorMA` is the per-object rule the row lacks.
- **G-open-1** (`general.cpp`): `InputString()`'s EOF branch under
  `-0q` returns an empty line to a caller that cannot tell EOF from an
  empty entry; A-3 is one consumer, and there may be others in the
  `NInputRange`/`RInputRange` family.

---

## S. `switch.cpp` -- reviewed 2026-09-12

4,290 lines: the whole command-switch surface as four tables and their
handlers (REFACTORING.md, "The registry as built"), `FProcessSwitches()`,
the `-Y2` sub-option packing, and the console build's `-W` consumer.
Every handler was read; the questions asked of each were whether its
argument reads are covered by an arity check, whether a stored value is
range-checked where a downstream consumer needs it to be, and whether
a suffix or prefix does something other than its `-H` line says.

### Checked and found fine

- **Arity, every handler.** For each of the 190-odd rows, the highest
  `argv[k]` a handler reads was compared against either its own
  `FErrorArgc()` call or the table's `carg`. They agree everywhere,
  including the shape-dependent ones (`-t`'s `2 - (i == 1) + (i < 0)`,
  `-q`'s six shapes, `-dp`'s `2-(i&1)`, `-E`'s `(ch1 == 'Y') + j`,
  `-r`'s `2 + 2*(...)`, `-~M/-~2/-~3`). The optional-argument handlers
  all test `argc > 1` before reading. `FErrorArgc()` (general.cpp:1565)
  requires `argc-1 >= n`, which is the convention the audit used. One
  consequence: `FProcessSwitches()` never sees a handler return more
  than it had, so the negative-`argc` loop that would follow is
  unreachable today; S-8 suggests the one-line guard anyway.
- **Table bounds.** `Yk` writes `kMainA[0..8]` into a `[9]`; `Yk0`/`Yk7`
  write `1..cRainbow` into `[cRainbow+1]`; `-YS` writes `rObjDiam[i]`
  under `FNorm(i)` into `[oNorm+1]`; `-YU` writes
  `szStarCustom[i-oNorm]` under `FStar(i)` into `[cStar+1]`; `-Yc` reads
  `szObjName[objMax+i]` for `i < 4` from `[objMax+4]`; `-Yq<n>` and
  `-Yi<n>` clamp their digit to `0..9` against `rgszLine[9]` (`-Yq9`
  fills 0..8) and `rgszPath[10]`. The `-YkO` row is the one exception
  and is A-2.
- **`-YRd 0` and negatives are accepted and are safe.** `nSignDiv` is a
  divisor at charts2.cpp:570, charts3.cpp:206 and xcharts1.cpp:3526, but
  the degree-change events those lines serve are only generated under
  `us.nSignDiv > 1` (charts3.cpp:357), so a zero never reaches an integer
  divide. Measured over `-d`, `-dm`, `-D`, `-Zd`, `-V`, `-Vy` and `-B`
  renders with `-YRd 0`, and `-dm` with `-YRd -1`: all exit 0.
- **The file-nesting guard works.** A chart file that `-i`s itself stops
  at `Settings files are nested more than 20 deep` (`cFileDepthMax`),
  exit 0. The macro route has no such guard: S-2.
- **`-YXj <n>`** stores `gs.cspace` unchecked, and every consumer guards
  `<= 0` before allocating or taking the modulus (xcharts1.cpp:2689,
  2819).
- **The `-X` lockdown rows** (`-Xb`, `-Xp`) test the *result* of the
  prefix rather than the prefix, so the `:Xb` line a saved file carries
  loads under `-0o`; the comment records the incident.
- **S-open-1 from the astrolog.cpp review is answered:** the `-YkO` row
  (switch.cpp:203) validates colour against `0..kMax-1` for every object
  `0..starLo`, which admits `kPlanet` for bodies `ObjOrbit()` returns
  `-1` for. The Moons dialog's `FValidColorMA` is the per-object rule
  the row lacks. Recorded under A-2; nothing more to add here.

### Findings

**S-1 -- `-4` followed by another switch swallows it.** *low-medium,
upstream, every build.*
**Status: fixed** — `NSwFour()` now tests `FNumCh(pin->argv[1][0])` like
its siblings instead of `NFromSz(...) >= 0`, so a following switch is
only read as the nesting level when it starts with a digit. Verified
with the review's repro (both orders now 55 lines), `-4` alone and
`-4 3 -g` unchanged, and `tools/chart-matrix.sh` and
`tools/switch-matrix.sh` byte-identical against the pre-change baseline
plus `make check` green.

    if (pin->argc > 1 && (i = NFromSz(pin->argv[1])) >= 0)
      darg++;

`NFromSz("-g")` is 0, 0 is `>= 0`, and `FValidDwad(0)` is true, so the
next switch is consumed as the nesting level and never runs. Measured:

    ./astrolog -qb 7 4 1976 12 0 8 122:19:55W 47:36:22N -g -4   55 lines (grid)
    ./astrolog -qb 7 4 1976 12 0 8 122:19:55W 47:36:22N -4 -g   23 lines (no grid)

Upstream's `case '4'` (astrolog.cpp:2627 in `5bf172e`) has the same
`>= 0`. Every sibling that takes an optional leading number tests for a
*nonzero* value or a digit first (`-w`, `-L`, `-N`, `-I`, `-d`, `-Xn`,
the day-arithmetic rows), so `-4` is the odd one out. `-H` documents it
as `-4 [<nest>]`, and the settings writer always writes the value
(`-4 %d`, io.cpp:1660), so a saved file is not affected; a command
line, a macro or the Enter Command Line box is.

*Fix shape:* the sibling test, `FNumCh(pin->argv[1][0])`, in place of
`>= 0`.
*Net:* the pair above, both 55 lines.

**S-2 -- A macro that runs itself recurses until the stack is gone.**
*medium, upstream, every build.*
**Status: fixed** — `FProcessCommandLine()` counts its nesting depth
(static counter, initial command line at 0; every route in — command
line, macro menu, AstroExpression `Switch()`/`Switch2()`, settings-file
line — passes through this one function) and refuses past
`cFileDepthMax` with the file guard's message shape, decrementing on
every exit past the guard so a refused line cannot leave the count
elevated. Verified: the review's repro exits 0 with
`Command lines are nested more than 20 deep ...`; the indirect cycle
`-M 1`→`-M 2`→`-M 1` and the AstroExpression cycle
`-~1 "Switch 1"` inside a macro are caught the same way; a 19-deep
chain of distinct macros still runs after a refused self-call in the
same process; chart and switch matrices byte-identical to baseline;
`make check` green.

    ./astrolog -Yi1 ephem -M0 1 "-M 1" -M 1 -n
    Segmentation fault, exit 139

`-M <n>` calls `FProcessCommandLine()` on the macro text, which calls
`FProcessSwitches()`, which reaches `NSwM()` again, with nothing
counting. Each level carries a `cchSzLine` buffer and a `MAXSWITCHES`
argv on the stack, which is the same reasoning `cFileDepthMax` gives for
bounding `-i` nesting at 20 -- and that guard was added to this fork for
exactly this shape of crash, one route over. Indirect cycles (`-M 1`
running `-M 2` running `-M 1`) are the same crash and harder to see in
a settings file. A macro can also be run from an AstroExpression and
from the GUI's macro menu, so the route is not command-line-only.

*Fix shape:* a depth counter in `FProcessCommandLine()` with the same
limit and the same message shape as the file guard.
*Net:* the line above, exit 0 with a message.

**S-3 -- `_X<flag>` turns graphics mode off, and the settings writer's
line order is what stops that from mattering.** *note, upstream.*

`NProcessSwitchTable()` applies `SwitchF2(us.fGraphics)` after every
`grfSwGraphics` row, and `FSwitchF2()` clears the flag under the `_`
prefix. Measured:

    ./astrolog -qb ... -Xb -Xo e.bmp =Xt      bitmap written
    ./astrolog -qb ... -Xb -Xo u.bmp _Xt      text chart, no file

Upstream does the same (`SwitchF2(us.fGraphics)` at astrolog.cpp:2873 in
`5bf172e`), so `_Xt` means "text mode, and no chart info" by design.
What makes it worth a line is the settings file: `FOutputSettings()`
writes every off flag as `_Xm`, `_Xu`, `_Xx0` and so on, each of which
turns graphics off on load, and then writes the real `=X`/`_X` line
*last* (io.cpp:2595; line 271 of `astrolog.as`, 377 of `nrvate.as`), so
the final state is right. Nothing at either end says the order is
load-bearing. Handed to the `io.cpp` review as I-open-1: a comment at
the writer, or a test that moves the line and watches graphics mode go.

**Status: fixed** — through I-open-1: the writer's comment now states
the load-bearing order and why (see the io.cpp section); the
round-trip assertion was rejected with reasons there, because the
settings-round-trip legs already exercise the flag and the writer
emits the master line last by construction.

**S-4 -- `-Ys <bad offset>` reports the error and then reports it
again.** *note, upstream.*

`NSwYs()` returns 0 after `FErrorValR()` fails, on purpose ("the retired
case continued here, consuming nothing"), so the rejected token is read
next as a switch and produces `Unknown switch` as well. Harmless;
listed so the strictness policy can decide whether the second message
stays.

**Status: closed** — the second message stays. The double report is the
documented consequence of "consume nothing on failure", which is the
stricter contract (a refused token is never silently swallowed), and
the two messages together say exactly what went wrong. Changing it
would trade one honest line for a special case in the dispatcher.

**S-5 -- Under `-0o` and `-0i`, `-o` and `-i` stop the line rather than
fail it.** *note, upstream.*

`NSwoCore()` and `NSwi()` return `nSwitchStop` after `ErrorArgv()`,
which `FProcessSwitches()` reads as success-and-stop, so everything
after the refused switch on that line is silently skipped and the run
proceeds. The comments say this preserves the retired case's
`return tcError` being read as success. A switch that is refused *and*
takes the rest of the line with it is an odd contract; recorded, not
proposed.

**Status: closed** — recorded only, per the finding's own bound. The
path requires `-0o`/`-0i` (refuse writes/reads) *and* one of those
switches on the same line, and the refused switch still prints its
error; a change here would alter the documented meaning of an
upstream contract for no measured victim.

**S-6 -- `-YY` payload switches drop a high byte instead of pushing it
back.** *note, upstream, negligible.*

`NSwYYLoad()` reads `getc()` into a `char` and only `ungetc()`s it when
`ch >= ' '`; a byte at or above 0x80 is negative in a signed `char` and
is dropped. The byte after a `-YY` argument is a newline in every file
that exists, so this has no victim today.

**Status: closed** — no victim exists and the format is ours to write;
recorded so a future reader of `NSwYYLoad()` knows the drop is known,
not missed.

**S-7 -- `-YRd` accepts zero and negatives silently.** *note, upstream.*

Not a crash (see the checked list), but `-YRd 0` means "no degree
events" with no message, where the help says `<n>` is a division count.
A `< 1` check like `-YQ`'s would say so.

**Status: fixed** — `-YRd` now refuses values below 1 with
`ErrorArgv()`, the same shape `-YQ` uses, so "0 means no degree events"
is stated by the program instead of assumed. Matrices byte-identical
(no net leg passes a sub-1 value; the review measured `-YRd 0` renders
as no-op), and a `-YRd 0` invocation now exits with an error message.

**S-8 -- Nothing in `FProcessSwitches()` refuses a handler that
consumes more than it was given.** *note, hardening.*

    argc -= i; argv += i;
    argc--; argv++;

with no test that `i < argc`. Today every handler is consistent (checked
list), so the loop cannot go negative. REFACTORING.md A2 names this
exact class as the mechanism behind the console build's settings-file
truncation; one `if (i >= argc) return fFalse;` after the call would
turn a future arity slip into an error instead of a read past `argv`.

**Status: fixed** — the guard is in: after a handler consumes, a count
greater than the arguments present makes `FProcessSwitches()` fail the
line with an error rather than walk `argv` past its end. Verified
byte-identical on both matrices (every handler is consistent today, so
the guard cannot fire on any legal input) and by a sabotage: one
deliberately over-consuming test handler made the unfixed loop read
past `argv` and makes the guarded one refuse.

### Handed to other files

- **I-open-1** (`io.cpp`): the `=X`/`_X` line must stay the last
  graphics line `FOutputSettings()` writes (S-3). Is that said anywhere
  at the writer, and is it pinned by a test?
- **G-open-2** (`general.cpp`): `NParseSz(sz, pmObject)` returning `-1`
  for a non-object is what makes `-1`/`-2`/`-XZ`'s optional argument
  safe; `NFromSz()` returning 0 is what makes `-4`'s unsafe (S-1). Worth
  confirming the `-1` is guaranteed for every non-matching string,
  including one that starts with a digit.

---

## I. `io.cpp` -- reviewed 2026-09-12

3,825 lines: `FileOpen()` and the two shared line readers, the switch-file
reader, seven file-format parsers and their writers, `FOutputSettings()`
(1,230 lines of it), the value parsers `NFromSz`/`NParseSz`/`RParseSz`,
the tty prompts, `FInputData()`, and the HTTP client with its JPL
Horizons cache. REFACTORING.md Area B (B1-B5) covered the reader
consolidation, the virtual filenames, the nested-include channel and the
round-trip legs; item 118's fixture group pinned every reader's
truncation point and found five overflow crashers on the way. What is
below is what those did not ask.

### Checked and found fine

- **`FileOpen()`** builds every candidate path through `S()`/`SO()`, the
  exe-relative rule for a `-Yi` directory is the documented one (a
  leading letter or digit and no `:` in second place), and the
  `szPath` return copies through a bounded `sprintf2`.
- **`FProcessSwitchFile()`**: the depth guard works (measured under
  S-2's checks: a self-including file stops at 20 with a message); the
  line buffer realloc-doubles; the Notepad `0xFF` trim is guarded by
  `i > 0`. Under `QT` it saves and restores `us.fGraphics` around the
  whole body, so **a settings file cannot change graphics mode in the
  Qt build at all** -- which is why S-3's writer-order dependency is a
  console and Windows matter only.
- **The writers guard NULL names.** `PrintQuotedSz()` writes nothing for
  NULL; the Quick*Chart and iCalendar writers use `SzSet()`/`FSzSet()`;
  the AAF writer's `pch` initialisation fix (a NULL dereference on an
  unnamed chart) is real and correct.
- **`FOutputData()`'s four-character object names do not collide on
  read-back.** `szObjName[]` has exactly two four-character prefix
  collisions, Titan/Titania and Alnath/Alnair, and upstream already
  widens those two (`oUrT`, `oAlr`) to `%-6.6s`. Measured: a `-o0` file
  with moons and stars on carries `Tita`, `Titani`, `Alna` and `Alnair`,
  and `NParseSz()` resolves each to its own object (`FMatchSz()` needs
  the whole of the shorter string to be a prefix). Not a finding; noted
  so nobody re-derives it.
- **`FOutputSettings()`'s string settings** are printed in pieces, never
  through `sz`, per CLAUDE.md; the `settings-strings` and
  `settings-fields` suite groups own that and were not re-run here.
- **The JPL URL encoder** checks that a `%27`/`%3B` expansion fits
  before shifting; the reply cache is a fixed 64-entry ring with bounded
  copies; a `-0n` run refuses before touching the network.
- **The iCalendar parser** checks `CchSz(pch) < 16` before reading
  sixteen fixed positions.
- **`InputString()`** takes its size (item 176) and gives back an empty
  string on EOF under `-0q`; the two range prompts pass `cchSzDef`.
- **The Solar Fire parser fed a blank line where the location belongs**
  (state 3) does not crash: measured under ASan, `Values in text file are
  out of range`, exit 0. The scan at that state is still I-4.

### Findings

**I-1 -- The old-style position reader smashes the stack on a long
token.** *medium-high, upstream, every build.*

`FInputData()` reads a file whose first character is `S` -- the `-o0`
position format versions before 5.40 wrote -- with

    fscanf(file, "%s%lf%lf%lf", sz, &k, &l, &m)      io.cpp:3493
    fscanf(file, "%s%lf%lf",    sz, &k, &l)          io.cpp:3498
    fscanf(file, "%s%lf%lf%lf", sz, &k, &l, &m)      io.cpp:3528

into `char sz[cchSzMax]`, with no width on the `%s`. Measured with a
file whose first token is 401 characters:

    printf 'S%0400d 1 2 3\n[D]: 1 2\n' 0 > pos-long.as
    ./astrolog -i pos-long.as -v
    Old style position file ended early.
    *** stack smashing detected ***: terminated        exit 134

    /tmp/astrolog-asan-review -i pos-long.as -v
    ERROR: AddressSanitizer: stack-buffer-overflow
    WRITE of size 402 ... in scanf_common

These are the only three `fscanf()` calls in the non-Swiss tree with a
`%s`. Item 143 bounded 1,141 *output* formatting calls and item 118's
fixture group pinned every hand-rolled reader; `scanf` is neither, which
is how it stayed. The route in is `-i <file>` on any file starting with
`S`, which includes File / Open in both GUIs, and the fortify canary is
the only thing between a hostile chart file and a controlled stack
write.

*Fix shape:* `%254s` (or a `cchSzMax-1` width built with the
preprocessor) at all three sites. Three tokens.
*Net:* the file above, exit 0 with `ended early`.

**Status: fixed** — a `cchSzScanMax` macro beside the size constants
builds the width into the format (`szFmtScanTok`), since scanf cannot
take the destination's size as an argument the way `S()`/`sprintf2()`
does; all three `fscanf` sites use it. Verified: the review's 401-token
file exits 0 with `ended early` (previously the fortify canary aborted,
and ASan reported the stack WRITE of size 402), chart and switch
matrices byte-identical, mingw clean.

**I-2 -- A Quick*Chart file whose last record has no line end loses that
record, silently.** *low-medium, upstream by choice.*

`FReadSzLineTrim()` returns `fFalse` "for a final line with no line end
of its own, which is what every caller's hand-rolled loop did before
it". For a one-record Quick*Chart file that is the whole file:

    ./astrolog -qb 7 4 1976 ... -zi "Quick Person" "Seattle, WA" -oq q.qc
    head -c -1 q.qc > q2.qc                        (strip the final \n)
    ./astrolog -i q.qc  -v   ->  chart for Quick Person
    ./astrolog -i q2.qc -v   ->  chart for Fri Nov 19, 1971 ... (the default)

No message: `FInputData()` sets `is.fHaveInfo` before the parser runs,
the parser returns success with nothing appended, and the chart that
was on screen stays. The Solar Fire reader takes the same path (its
last needed line is the location line), and the iCalendar one is safe
only because `END:VCALENDAR` follows the last event. The fixture group
pins the truncation *points*, not this.

*Fix shape:* return `fTrue` when `fgets()` delivered anything, and
`fFalse` only when it delivered nothing -- the `feof()` test moves to
the *next* call. The consolidation comment says the old behaviour was
kept on purpose; it is the maintainer's call whether "what the
hand-rolled loops did" is worth keeping over a lost record.
*Net:* the pair above, both `Quick Person`.

**Status: fixed** — the maintainer's call was to fix it: the reader
returns `fFalse` only when `fgets()` delivered nothing, so a final
line with no line end of its own is delivered like any other and the
next call reports end of file. All three callers (Quick*Chart, Solar
Fire, iCalendar) already treat `fFalse` as end-of-file, so the record
lands in the state machine it belongs to. Verified: the review's pair
above both read `Quick Person`, a stripped iCalendar still loads, the
chart and switch matrices are byte-identical (every file the nets
exercise carries a trailing newline), suite 5191/0 and the full suite
under ASan clean, mingw clean.

**I-3 -- The skip-reader and the switch-file loop compare a signed
`char` to a space.** *note, upstream.*

`FReadSzLineSkip()` (io.cpp:295) and `FProcessSwitchFile()`'s line loop
(io.cpp:383) both do `(ch = getc(file)) < ' '` with `char ch`, so a
byte at or above 0x80 reads as negative and is skipped as if it were a
control character -- but only for the *first* byte of a line; the copy
loops that follow cast to `uchar`. No format this reads has a line that
legitimately starts with a non-ASCII byte (AAF lines start with `#`,
ADB with `<` or whitespace, settings lines with a prefix character), so
there is no victim; the pattern is worth knowing before a new reader
copies it.

**Status: closed** — recorded as a pattern hazard, not changed: no
readable format has a line starting with a non-ASCII byte, and the
`uchar` casts downstream are the correct form a new reader should
copy.

**I-4 -- The Solar Fire parser's location scan runs past the terminator
on an empty line.** *low, upstream.*

    for (pch2 = szLine; *pch2 <= ' '; pch2++)      io.cpp:1199 (state 3)

has no `*pch2 &&`, unlike the identical scan two states earlier. A blank
line at that state (a malformed export) walks past the NUL into whatever
the previous line left in the 1,020-byte buffer, until it meets a byte
above space. Measured clean under ASan, which is expected: the walk
stays inside one stack object and ASan only sees its edges. It leaves
the object only if every remaining byte is at or below space, which
uninitialised stack rarely is -- bounded by luck rather than by code.
*Fix shape:* the missing `*pch2 &&`.

**Status: fixed** — the scan now stops at the terminator, matching the
identical scan two states earlier. The blank-line-at-state-3 input
exits 0 with a clean format error, clean under ASan (the review
measured the walk as bounded by luck, not code; now it is bounded by
the `*pch2 &&`).

**I-5 -- The JPL Horizons name copy has no bound.** *low-medium, this
fork's client, network input.*

    for (pch = szLine+18; *pch && !(pch[0] == ' ' && ...); pch++)
      szName[i++] = *pch;                             io.cpp:3781

copies from a `cchSzLine` (1,020) line into `char szName[cchSzMax]`
(255). A `Target body name:` line longer than 254 characters before a
double space overflows the stack. Horizons has never sent one, but the
bytes come from the network, through Qt's TLS client here and through
`wget` on the X11 build, and a stack write from a reply is the wrong
thing to leave to the server. `FJPLCachePut()` right below it bounds
its copies.
*Fix shape:* `i < cchSzMax-1` in the loop condition.

**Status: fixed** — the loop condition carries `i < cchSzMax-1`, so a
hostile or oversized `Target body name:` line truncates at the buffer
the way every other name copy in the file does. Verified by reading
(the loop is the only unbounded writer into `szName`) and by clean
builds; the network path itself cannot be driven offline, so the
before/after is the code shape, not a caught report.

**I-6 -- `NParseSz(pmColor)`'s `#rrggbb` branch accumulates into a
leftover `i`.** *note, upstream.*

`i` holds `cAspect+1` from the aspect-name loop when the `#` branch
starts, and the three `(i << 8) | byte` steps carry that into bits
24-31 before `Rgb(RgbB(i), RgbG(i), RgbR(i))` masks it away. Correct
by the mask, not by intent; the `pmRGB` branch below it sets `i = 0`
first. One assignment.

**Status: fixed** — the `#rrggbb` branch sets `i = 0` first, matching
the `pmRGB` branch below it; the value was right by the mask and is
now right by construction. Byte-identical matrices (the accumulated
bits never survived the mask), one assignment.

**I-open-1 from the switch.cpp review, answered:** nothing at
io.cpp:2595 says the `=X`/`_X` line must be the last graphics line
written, and nothing tests it. The line has been last since upstream
wrote it, every `_X<flag>` line above it clears graphics mode on load
(S-3), and the Qt build is immune only because `FProcessSwitchFile()`
restores the flag around a file. A one-line comment at the writer and
a round-trip assertion that a saved file loads with graphics mode
unchanged in the console build would close it. Recorded here rather
than as a new number: it is S-3's consequence, not a separate defect.

**Status: fixed** (comment half) — the writer now says the line order
is load-bearing and why: every `_X<flag>` line above clears
`us.fGraphics` on load in the console and Windows builds, so the
master `=X`/`_X` line must stay last; the Qt reader is immune because
it restores the flag around the whole file. The round-trip assertion
was not added: `settings-round-trip`'s flip-every-boolean leg already
flips graphics mode and re-saves, and a saved file round-trips with
the master line last by construction of this writer, so a test would
pin the comment, not the code. S-3 closes with this.

### Handed to other files

- **G-open-2** stands: `FMatchSz()` (general.cpp:153) requires the
  whole of its first string to be a prefix of the second, with a
  three-character minimum unless the strings are equal -- so a
  three-character abbreviation resolves to the *first* table entry it
  prefixes. `NParseSz(pmObject)` returns `-1` for a non-digit string
  that matches nothing, which is what keeps `-1`/`-2`/`-XZ` safe (S-1).

---

## G. `general.cpp` -- reviewed 2026-09-12

3,554 lines: string and comparison helpers, the maths helpers, the
rulership and ray tables' derived state, date arithmetic (`AddTime()`,
`GetTimeNow()`, the DST autodetect), `PrintSz()` and the message
family, the switch error helpers, `AnsiColor()`, the `Sz*()` formatters
with their static buffers, the chart list (append, sort, filter,
enumerate), the UTF-8/Latin/IBM conversions, display-name and clone
ownership, the counted allocator and `Terminate()`.

### Checked and found fine

- **Every `Sz*()` static buffer** was checked against its widest format:
  `SzZodiac[32]`, `SzAltitude[15]`, `SzDegree[15]`, `SzDegree2[15]`,
  `SzHMS[24]`, `SzTimeR[40]`, `SzZone[20]`, `SzOffset[24]`,
  `SzLocation[768]`, `SzElevation/Temperature/Length[21]`,
  `SzColor/SzColor2/SzColorHTML[8]`. Every write is `sprintf2` through
  `S()` or `SO()` and the fixed offsets (7, 9, 10) land where the
  preceding format ends. `SzAltitude` with seconds and milliseconds
  fills exactly 14 characters plus the terminator. The one buffer a
  legal value can outgrow is G-4, and that format is unused.
- **`FormatR()`'s trailing-zero strip** would turn `100` into `1` for
  `n == 0` (it strips zeros whether or not a decimal point was printed),
  but no caller passes 0: the precisions in use are -6, -5, -4, -2, -1,
  5 and 6. Worth a guard if a caller ever does.
- **`FMatchSz()`** requires the whole of its first argument to prefix
  the second, with a three-character minimum unless equal; `SzLookup()`
  applies the same rule. **G-open-2 answered.**
- **`AddTime()`** carries seconds through millennia with its overflow
  chain intact; `AddDay()` and `DaysInMonth()` handle the 1582
  changeover; `DayInMonth()` applies Julian leap years before it.
- **`RDstAutoDetect()`** asks the atlas for the location's rule and only
  falls back to the host's clock; both `GetTimeNow()` branches call it
  (the fix its comment records is real).
- **`FilterCIList()`** guards NULL names and locations (the crash it
  records is real); `FAppendCIList()` grows by 500 with a bounded copy;
  the shell-sort gaps are the A102549 sequence.
- **`PrintSzFormat()`**'s walk is bounded one short of the terminator.
- **`FErrorValR()`** has a `cchSzLine*2` buffer for the reason its
  comment gives (`1e308` formats to 300+ characters).
- **`Terminate()`**'s re-entrancy latch and the `PrintSz()` pager's
  `feof()` guard are independent, as documented.
- **Colour indexes reaching `AnsiColor()`/`SzColorHTML()`** are resolved
  palette indexes or negative RGB: `kObjA[]` is resolved by
  `InitColors()`, `kAspA`/`kRayA`/`kMainA`/`kElemA` are range-checked at
  their switches, so the `szColorHTML[cColor2]` and `rgbbmp[cColor2]`
  reads never see `kElement..kPlanet` (26-29). A-2 is the one resolution
  that fails.
- **`SzClone()` strings are uncounted on purpose.** It allocates through
  `PAllocate()` and then reverses the three counters, so chart names,
  locations and the `-zi`/`-zj` strings are never freed and never
  reported. That is why the `is.cAlloc` check in section A stayed
  silent with a named chart: it cannot see these. Bounded by session
  length; noted so nobody reads that silence as proof of no leaks.
- **G-open-1 answered:** the consumers of `InputString()`'s EOF empty
  string are `NInputRange()`/`RInputRange()` (which loop until the
  value is in range, so under `-0q` at EOF they loop forever: that is
  A-3's engine), `NPromptSwitches()` (an empty command line, harmless)
  and the two name prompts in `FInputData("tty")` (an empty name,
  harmless). Nothing else calls it.

### Findings

**G-1 -- `FEnumerateCIList()` on an empty chart list dereferences NULL.**
*medium, upstream, both builds.*
**Status: fixed** — early `return fFalse` on `is.cci < 1` after the
`~5Y` hook test, matching `FSortCIList()`'s guard above; the do-loop
below always runs its body once. Verified: the repro command segfaulted
(exit 139) before and exits 0 after; chart and switch matrices
byte-identical against the parent-commit baseline.

    do {
      ...
      ciCore = ciMain = is.rgci[iList];          general.cpp:2789
      ...
    } while (iList < is.cci);

runs its body once whatever `is.cci` is, and `is.rgci` is NULL until the
first chart is appended. Measured:

    ./astrolog -Yi1 ephem -~5Y "1" -Y5 -n
    Segmentation fault, exit 139
    (ASan: SEGV on address 0x0 in FEnumerateCIList, general.cpp:2789,
     called from NSwY5, switch.cpp:1030)

The route needs the `~5Y` hook set -- a settings-file line -- and then
any `-Y5`/`-5Y` before a chart list exists. `Action()`'s own `-5e` loop
guards with `is.cci > 0`; this one does not. `FSortCIList()` right above
it returns early on `is.cci <= 1`.

*Fix shape:* `if (is.cci < 1) return fFalse;` after the hook test.
*Net:* the line above, exit 0.

**G-2 -- `UTF8ToWch()` reads the third byte before it has checked the
second.** *low, upstream, latent.*
**Status: fixed** -- the `pch[2]` read now sits inside the
`(ch1 & 0xf0) == 0xe0 && (ch2 & 0xc0) == 0x80` test, so `ch3` is only
read once `ch2` has passed the continuation-byte check. Verified with an
ASan driver linked against the real objects calling `CwchSz()` on a
`SzClone()` whose last byte is a lone three-byte lead: heap-buffer-overflow
READ of size 1 in `UTF8ToWch` before, clean run (and correct decoding of
1/2/3-byte sequences) after; chart and switch matrices byte-identical
against the parent-commit baseline.

    ch2 = pch[1];
    if ((ch1 & 0xe0) == 0xc0 && (ch2 & 0xc0) == 0x80) { ...; return 2; }
    ch3 = pch[2];
    if ((ch1 & 0xf0) == 0xe0 && (ch2 & 0xc0) == 0x80 && ...

When a string ends in a lone three-byte lead (`0xE0..0xEF`) -- a Latin-1
`é` read under the UTF-8 charset, say -- `pch[1]` is the terminator and
`pch[2]` is one byte past it. Every caller reached here passed a string
inside a larger buffer, so ASan saw nothing on `-Ya3 -zi $'Caf\xe9'`;
`CwchSz()` on a clone whose last byte is such a lead would read one
byte past the allocation. One clause moved: test `ch2` before reading
`ch3`.

**G-3 -- `NCompareSz()` and `NCompareSzI()` answer backwards for NULL.**
*note, upstream.*

    if (sz1 == NULL || sz2 == NULL)
      return (sz1 == sz2);

returns 0 ("equal") when exactly one side is NULL and 1 ("different")
when both are. `FSortCIList()` methods 3 and 4 compare chart names and
locations, which are NULL on never-named slots, so a NULL name sorts as
equal to any name and two NULL names as unequal to each other. No
crash, an inconsistent comparator; the sort is not stable anyway.

**Status: closed** — recorded, not changed. The comparator's contract
is "non-NULL compares by content"; NULL handling is outside it, and
every NULL-adjacent consumer (`FilterCIList`, `FAppendCIList`) already
guards NULL before comparing. Making the comparator NULL-total would
define an ordering the sort's callers have never asked for; the
inconsistency is bounded by a sort that is not stable anyway.

**G-4 -- `SzDate()` format 1 cannot hold a seven-digit year.** *note,
upstream, unused.*

`static char szDat[20]` and `"%s %d, %d"` with `September 30, -9999999`
(the `FValidYea` extreme) needs 23. `sprintf2` truncates the year
silently. No caller passes format 1 today (the codes in use are 2, 3,
and the flag-valued forms), so this is a trap for the next caller.

**Status: closed** — no caller exists, so there is nothing to fix
without inventing one; the buffer would grow on the first caller's
commit instead. The finding stands as the trap marker it was written
to be.

**G-5 -- `PAllocate()` under `DEBUG` writes the canary before testing for
NULL.** *note, DEBUG builds only.*
**Status: fixed** -- the canary block now returns NULL when `pb` is NULL
before writing the sentinels, so an out-of-memory in a DEBUG (_DEBUG/MSVC)
build reports the warning and returns NULL like the non-DEBUG build
instead of dereferencing NULL. Verified by reading: no makefile defines
DEBUG on this platform (it rides on MSVC's `_DEBUG`), so there is no
crashing build here; the non-DEBUG path and normal output are untouched
(chart and switch matrices byte-identical against the parent-commit
baseline).

The `pb == NULL` branch prints a warning and then falls into
`*(dword *)pb = dwCanary`. An out-of-memory in a debug build is a
NULL write rather than the warning. Return after the warning.

### Handed to other files

Nothing new. The `-0q` loop (A-3) and the `-YkO` row (A-2) are the two
findings from other files whose mechanism lives here, and both are
closed against this file's code above.

---

## C. `calc.cpp` -- reviewed 2026-09-12

4,433 lines: Julian day fronts, the house systems Astrolog computes
itself and the partition check over all of them, star and planet
computation (`ComputeEphem()`, `CastChart()` and its seven extracted
phases, `CastSectors()`), the aspect/parallel/distance matchers and the
two grids, the eclipse geometry, the ephemeris search-path list, the
custom-object (OBJDEF) store and its three parsers, and the Swiss
Ephemeris wrapper layer. REFACTORING.md Area C (C1-C6) covered the
structure and the cooked-`ciCore` contract; the numeric oracle covers
the numbers. This pass asked about edges: divisions, buffers handed to
Swiss, and state left behind on failure paths.

### Checked and found fine

- **The house-width divisions cannot see a zero.** `ApplyDomalFlip()`
  and `RHousePlaceIn3D()` divide by the width of the house an object is
  in. The Sinusoidal Delta family collapses two cusps onto one degree on
  purpose in a narrow quadrant (`FEnsureHousePartition()` exempts it),
  but a zero-width house cannot contain an object -- `NHousePlaceIn()`'s
  `>= cusp && < next` is never true for equal cusps -- so the divisor is
  never that house. Measured to be sure: 48 casts each with `-f` and
  `-c3` under the Swiss engine and again under the Matrix engine (`_b`),
  latitudes 70/80/88, every three hours on the solstice, no NaN, no Inf,
  no sanitizer report.
- **`FEnsureHousePartition()`** checks the result rather than a list of
  systems and its 0.001-degree threshold is the oracle's; the Sunshine
  finding it records against the vendored library is real (the polar
  guard is commented out in both solutions).
- **`CastSectors()`** bounds every event array by `MAXINDAY`, and the
  bracket search's `fFound` fix is correct: an object with a single event
  is restricted rather than placed at zero.
- **The eclipse geometry** guards every object index with `FNorm()`
  before reading `rObjDiam[]` (the star case its comments record), and
  its only divisions are by distances that cannot be zero between
  distinct bodies.
- **`ComputeEphem()`'s velocity normalisation** divides `ret[i]` by
  `rDegMax / (rObjYear[j] * rDayInYear)`; a zero year (the Sun's entry)
  makes the divisor infinite and the velocity zero, not NaN.
- **The ephemeris path list** (`EDL`) is bounded at Swiss's own 20
  entries and 242 characters, deduplicates with trailing separators
  ignored, keeps user-named directories through a squeeze, and warns
  only when a user named something that held nothing. Each of the three
  failures its comment describes is real in the Swiss source.
- **The asteroid enumeration** skips a missing body instead of ending
  (the `-XE` inertness incident), steps `iast` on the success path (the
  120-second hang), and records the body's own number in `pes->nAst`.
- **The OBJDEF parsers** share one definition parse with the
  `FObjSelFlagRun()` guard against reading a name as flags; the
  seen-name cache is a fixed 256-entry table with bounded copies.
- **`SwissComputeStarSort()`'s pointer-to-offset trick** survives the
  sort because every pointer is rebased from `sz`; the `< cchSzDef`
  test on a pointer distinguishes an offset from the static `""` it
  uses for no designation.
- **`FSwissPlanet()`'s `swe_set_topo(-OO, AA, ...)`** reads the cooked
  latitude, which is the documented contract.
- **Star name write-back is within bounds:** `swe_fixstar2()` writes at
  most `SWI_STAR_LENGTH + 2` (42) bytes back into the caller's buffer
  and every caller passes `cchSzDef` (80). `SE_MAX_STNAME` (256) in the
  public header overstates it.

### Findings

**C-1 -- `FSwissPlanet()` can return with a custom body's flag
inversions still applied.** *low-medium, upstream, by reading.*
**Status: fixed** -- the three `return fFalse`s in the unusual
central-object branch now set a failure flag and `goto` one `LRestore`
label placed at the restore block, so the five inverted `us.` settings
are undone on every exit; the restore still precedes the position write
that reads `us.fSidereal`, and the flag return sits before the `nRet`
check so the goto paths never read it unset. Verified by reading every
exit path, and by the behaviour net: chart and switch matrices
byte-identical against the pre-change build (the route itself still
needs moon-centre ephemeris files the bundle lacks, so the failure
paths are exercised by inspection, not execution).

A custom slot's definition flags (`-Ye...HSBNTV`) are applied by
inverting `us.fSidereal`, `us.fBarycenter`, `us.fTrueNode`,
`us.fTruePos` and `us.fTopoPos` in place at calc.cpp:3576-3582, and
restored at 3646-3652. Between the two sit three `return fFalse`s in
the "unusual central object" branch (calc.cpp:3606-3625): a central
custom slot whose type-2 body `FSwissFromObj()` cannot map (a star), a
central slot that is not a custom object at all, and the
"customized to be a COB" case where `iobj == iobjCent`. Any of them
leaves the five settings inverted for the rest of the session, and
since the same call runs once per cast, the state then alternates on
every chart. The route is a planet-centred chart (`-h`) whose centre is
a custom slot, with another slot carrying a flag; measured attempts
(`-Yem Vulcan 599 -YemS Cupido 599 -h Vulcan`, and a star-centred
variant) computed nothing at all here because moon centres of body
need ephemeris files the bundle does not carry, so the effect is
asserted from the code, not observed.

*Fix shape:* one cleanup label; the three returns jump to it.
*Net:* the `-h Vulcan` line above with the moon files present,
casting twice, and asserting the Sun's longitude is the same both
times.

**C-2 -- Body names from Swiss are written into 80-byte buffers that
Swiss may fill to 255.** *low, latent, this fork's callers.*

`SwissGetObjName()` hands its caller's buffer straight to
`swe_get_planet_name()` and only afterwards bounds the "not found"
rewrite. For an asteroid, Swiss copies the name from the `.se1` header
(50 bytes) or, failing that, a whole line of `seasnam.txt` read with
`fgets(si, AS_MAXCH)` and `strcpy(s, sp)` -- up to 255 bytes; for a
fictitious body, the name field of `seorbel.txt`. The callers pass
`cchSzDef` (80): `switch.cpp:633` (`-Ye`), and three `SzObjSelName()`
sites in `qtdialog.cpp` (5095, 5195 and one more with `cchSzMax`);
`wdialog.cpp` passes `cchSzMax`. The shipped data files have no name
over 40 characters, so nothing overflows today; a user-edited
`seasnam.txt` or `seorbel.txt` line would. `swe_get_planet_name()` has
no size parameter, so the honest fix is a local `AS_MAXCH` buffer in
`SwissGetObjName()` copied out through `cchMax`.
**Status: fixed** -- the lookup now fills a local `AS_MAXCH` (256)
buffer and copies out through the caller's `cchMax`, so the worst case
is truncation at the caller's own bound, never an overrun; the
"not found" rewrite runs on the local buffer before the copy. The
smallest bound any caller passes is `cchSzDef` (80: the `-Ye` switch
handler, the qttest table check, and one Object Selections dialog
buffer); every other site passes `cchSzMax`. Verified by reading the
vendored `sweph.cpp` (whole `fgets(AS_MAXCH)` line of `seasnam.txt`
copied out) and `swemplan.cpp` (`seorbel.txt` names the same way), and
by the behaviour net: chart and switch matrices byte-identical against
the pre-change build; make check passes (5191 passed, 0 failed).

**C-3 -- The sorted star enumeration caps at 1,500 and the catalogue is
at 1,360.** *note.*

`SwissComputeStarSort()` allocates 1,500 entries and stops filling at
that many; `sefstars.txt` as shipped has 1,360 non-comment lines. A
catalogue extended by 141 stars would silently drop the rest from every
sorted (`-Un`, `-Ub`, ...) enumeration while the unsorted one still
showed them. One constant, or size from the file.
**Status: fixed** -- a cheap cap raise: the literal 1500 (upstream, in
the tree since the 7.80 file upload) became `cStarSortMax` 2000 with a
comment recording the measured catalogue (1360 non-comment lines in the
shipped `sefstars.txt`) and the truncation failure it guards. Sizing
from the file was rejected as not worth a second pass; 2000 restores
the same ~140-over-1360 headroom. Verified by the behaviour net: chart
and switch matrices byte-identical against the pre-change build (the
enumeration stops at the catalogue's own end either way, so today's
output cannot move); make check passes (5191 passed, 0 failed).

### Handed to other files

- **M-open-1** (`matrix.cpp`): `ComputeHouses()`'s Placidus/Koch polar
  refusal keys on `is.OB` and the pole; the Matrix `HousePlacidus()`
  and `HouseKoch()` are reached only past it. Whether the Matrix
  Campanus/Regiomontanus/Topocentric functions produce the multi-circle
  cusps `FEnsureHousePartition()` describes, and so always take the
  Porphyry fallback near the pole, is a Matrix-side question.

---

## M. `matrix.cpp` -- reviewed 2026-09-12

718 lines: the built-in Matrix maths -- Julian day conversions, the
obliquity and sidereal offset, seven house systems, Kepler-solved
planet positions with the outer-planet error series, and the lunar
series. Reached only behind the `FCm*()` backend predicates, when
ephemeris files are off. REFACTORING.md Area C gave it a clean verdict
and it keeps it.

### Checked and found fine

- **Julian conversions** use `Dvd()` throughout, so years before 4800 BC
  floor rather than truncate; the reverse conversion has always.
- **`CuspPlacidus()`'s `RAcos()`** is the one NaN source in the file (its
  argument exceeds 1 near the pole); `ComputeHouses()` refuses Placidus
  and Koch inside the polar circle before it, and `FEnsureHousePartition()`
  catches anything that slips through, on the result rather than a list.
- **`CuspTopocentric()`'s `RTan(latR)/RCos(OA)`** goes infinite at
  `OA = ±90°` and `RAtn()` of it is finite; `HouseTopocentric()` takes
  its pole latitudes as parameters now (item 113), not through `AA`.
- **`ComputePlanets()`'s stride** skips the Moon and Vulcan exactly as
  its comment says; the velocity divisor `XS²+YS²` is zero only for the
  Sun, which the loop starts on and zeroes afterwards; `helioret[]` is
  filled for every index the second pass divides by.
- **`ErrorCorrect()`'s table walk** consumes `3 + 3*cErrorCount` reals
  per component from offsets that sum to the table's declared size
  (72+51+42*3), Jupiter's third component excepted as coded.
- **Measured:** every Matrix-backend house system (`-c 0..12` under
  `_b`) at 89N, 89S and 66N, at 0h and 12h -- 78 casts -- exit 0, no
  NaN, no Inf. **M-open-1 answered:** near the pole the Matrix systems
  either compute finite cusps or fall to Porphyry through the partition
  check; none produces a multi-circle set the check misses.

### Findings

None.

---

## D0. `charts0.cpp` -- reviewed 2026-09-12

2,075 lines: the credits box, the `-H`/`-Y` help text and its colouriser
`PrintS()`, the `-HO`/`-HA`/`-HC`/`-HF`/`-HS`/`-H7` tables, the Arabic
parts computation and listing, the graphics key list, and
`FPrintTables()`. The help text itself is checked by
`tools/registry_audit.py` (every spelling it documents resolves) and
was not re-read line by line.

### Checked and found fine

- **`PrintS()`'s colour state** is the static three-character memory its
  D3 note describes; the `_` to `chSwitch` rewrite is the only text
  transformation.
- **`PrintAspects()`'s zero-strip** (`while (*(--pch) == '0')`) runs on
  a `%6.2f` that always has a decimal point, so it cannot walk back into
  the name; the `PrintTab(' ', 3-CchSz(sz))` with a negative count is a
  no-op by `PrintTab()`'s `while (cch-- > 0)`.
- **`PrintSigns()`** reads `szObjDisp[rules[SIGT(i)]]` unguarded, and
  `rules[]` is never `-1`: `AdjustRulership()` only demotes a secondary
  ruler, and a primary whose planet is set to rule nothing keeps the
  sign (the D2 note's "entries never empty" holds by construction).
- **`GetSzGenitive()`** appends from a compiled instruction table into a
  `cchSzDef` buffer whose contents are constellation names; the one
  `sprintf2` is bounded.
- **`ComputeArabic()`** indexes `chouse[]`, `inhouse[]` and `planet[]`
  with two-digit numbers parsed from the compiled formula table; the
  recursion for Fortune/Spirit is on a fixed negative index.
- **`FPrintTables()`** under `WIN` returns early in graphics mode; the
  Qt build reaches the same tables through its own `-H` handling.

### Findings

**D0-1 -- `-HO` and `-HS` test restrictions on the unsorted index and
print the sorted object.** *low-medium, upstream, both builds.*

    // PrintObjects(), star loop:
    for (i = starLo; i <= starHi; i++) if (!ignore[i]) {
      j = rgobjList[i]; ... print j ...
    // PrintOrbit():
    for (i = 0; i <= is.nObj; i++) {
      if (ignore[i] || !FThing(i)) continue;
      j = rgobjList[i]; ... print j ...

`rgobjList[]` is the display order, which a star sort (`-Un`, `-Ub`,
...) or a `~v` AstroExpression permutes; the restriction of slot `i` is
then applied to whichever object sorts into position `i`. Measured with
stars sorted by name and Sirius restricted:

    -HO =U =Un              100 star rows
    -HO =U =Un -R Sirius     98 star rows; Achernar missing, Sirius present
    -HS =U =Un              119 rows
    -HS =U =Un -R Sirius    117 rows; Achernar and Sirius both missing

`ChartListing()` and `PrintObjects()`'s own planet loop get it right
(`j = rgobjList[l]; if (ignore[j])`), which is the fix shape for both.
*Net:* the two pairs above, Sirius the only row that differs.

**Status: fixed** — both loops now fetch `j = rgobjList[i]` first and test
`ignore[j]` (PrintOrbit also tests `FThing(j)`) on the object, not the
slot. Reproduced first: `-HO =U =Un -R Sirius` dropped Achernar's row and
printed Sirius with a blank position (0Ari00), `-HS` likewise blanked
Sirius's distance and dropped Acherna's. After the fix both switches drop
exactly the Sirius rows (the -HO/-HS table row, the -HS orbit row and the
listing's `Siri:` row, whose element-table tallies shift by one with it)
and Achernar stays. Chart and switch matrices byte-identical to the
baseline (neither has a star-sort-plus-restriction leg; the targeted
before/after diffs carry the trigger path).

**D0-2 -- A sorted Arabic parts interpretation pairs each part's name
with another part's position.** *medium, upstream, both builds.*

    for (h = i = 0; i < cPart; i++) {
      l = iPart[i];                                   // sorted index
      ...
      InterpretArabic(l, rPart[i], rLat[i], rDir[i]);  // unsorted values

Every other use in the loop is `rPart[l]`. Measured on the Part of
Fortune, which the plain listing puts at 9Cap13 in the 3rd house:

    -P -I     Part of Fortune in Capricorn and 3rd House: ...
    -Pn -I    Part of Fortune in Scorpio and 2nd House: ...

so `-P[z,n,f] -I` writes an interpretation of the wrong placement for
every part but the ones the sort leaves in place. Three subscripts.
*Net:* the pair above, same sign and house.

**Status: fixed** — the call reads `InterpretArabic(l, rPart[l],
rLat[l], rDir[l])`, matching every other use in the loop. Reproduced
first on the Part of Fortune (10Tau55, 12th house in the plain
listing): `-P -I` interpreted Taurus/12th, `-Pn -I` Aries/11th. After
the fix `-P`, `-Pn`, `-Pz` and `-Pf` with `-I` all interpret
Taurus/12th, and the full `-P -I` and `-Pn -I` outputs are identical as
multisets. Chart and switch matrices byte-identical to the baseline
(the matrix's `-I` legs carry no Arabic parts).

---

## D1. `charts1.cpp` -- reviewed 2026-09-12

3,120 lines: the chart header, the `-v` listing, the aspect/midpoint
grid and its configurations, the text wheel, the aspect and midpoint
lists (one core each since items 82-83), aspects-to-a-point, the local
horizon, orbit (`-S`), esoteric, sector, astrocartography (one core
since item 101) and planetary-moons charts, and `PrintChart()`.

### Checked and found fine

- **`PrintHeader()` and `PrintWheelCenter()`** take `cchSzLine` for the
  reasons their comments record (an atlas location name alone passes 58
  characters); `PrintWheelCenter()`'s centring is a no-op when the text
  is wider than the wheel, not an overflow.
- **`ChartListing()`'s combined loop** reads `rgobjList[l]` for `l` up
  to `oNorm+20`, inside the array's `objMax` extent, before replacing it
  with `oNorm` for the cusp and element rows.
- **`ChartWheel()`** cannot loop forever in its overflow-to-next-house
  scan: `count < nWheelRows*12` ends the outer loop exactly when every
  slot is taken, and `FValidWheel` bounds `nWheelRows` by `WHEELROWS`.
- **`ChartSector()`'s `sec > cSector` wrap** and `CastSectors()`'s
  `fFound` are the fixes their comments describe; `rgc[]` and
  `pluszone[]` are indexed only after the wrap.
- **`ChartAstroGraphCore()`** bounds every `rgcr[]` write by
  `cCross < MAXCROSS`, including the AstroExpression un-append; the
  `objMax*i3` encoding of "which ring" is undone symmetrically at print.
- **`ChartMoons()`** saves and restores `ignore[]`, `us.objCenter`,
  `us.fMoonMove` and `us.fInterpret` on every path that reaches the end;
  its two `return fFalse` paths (grid allocation failure) skip the
  restore, which is the same negligible class as the note under
  `PrintChart()` below.
- **`PrintGridCell()`'s in-place `%` overwrite** lands on a character
  inside the `%c%f` result at both offsets.
- **`ChartHorizon()`**'s `azi[]`/`alt[]` are `objMax` and indexed by
  `i <= is.nObj`; `PrintHorizonLine()` divides by whichever of `sx`/`sy`
  is larger in magnitude, which cannot be zero.

### Findings

**D1-1 -- The orbit chart prints the wrong star's brightness under a
star sort.** *low-medium, upstream, both builds.*

    for (j = 0; j <= is.nObj; j++) {
      i = rgobjList[j];
      ...
      if (FStar(i)) {
        sprintf2(S(sz), " %5.2f", rStarBright[j-oNorm]);   // j, not i

Every other field on the row uses `i`. Measured with `-S =U`, Sirius
row, brightness column:

    unsorted      Star # 1 -1.46
    =Un (by name) Star # 1  2.21

The `-v` listing next to it prints `rStarBright[j-oNorm]` with its own
`j` being the object, and gets -1.46. Same class as D0-1 (position
index used where the object index belongs); one subscript.
*Net:* the pair above, same sign and house.

**Status: fixed** — the orbit chart's brightness column reads
`rStarBright[i-oNorm]`, matching every other field on the row; the
`-v` listing's own `j` is the object there and was left alone.
Reproduced first: `-S =U =Un` showed `Ache ... -1.46` and
`Siri ... 2.21`; after the fix Sirius reads -1.46 and Achernar 0.46
under both `=U` and `=U =Un`. Chart and switch matrices byte-identical
to the baseline (no matrix leg renders a star-sorted orbit chart).

**D1-2 -- `PrintChart()`'s grid block leaves two settings changed if
the grid cannot be allocated.** *note, upstream, negligible.*

    fCall = us.fSmartCusp; us.fSmartCusp = fFalse;
    nSav = us.objRequire; us.objRequire = -1;
    if (!FCreateGrid(fFalse))
      return;                               // restore skipped
    us.fSmartCusp = fCall; us.objRequire = nSav;

`FCreateGrid()` fails only when `PAllocate()` does, once per process
(the grid is allocated once and kept), so the exposure is an
out-of-memory at first chart. The `Borrow` the `WIN` block above it
already uses is the fix shape.

**Status: fixed** — the early return now restores both settings before
returning, the same two-line restore the success path below it runs.
Unreachable in practice except under memory exhaustion (the grid is
allocated once and kept), so the net is reading plus byte-identical
matrices; the change only touches a path that today ends the chart
output with state corrupted.

---

## D2. `charts2.cpp` -- reviewed 2026-09-12

1,234 lines: the multi-chart listing and relationship grid,
`CastRelation()`, the shared `PrintInDayEvent()`/`PrintAspect()`
formatters, the `-D` and `-T` influence lists, `EclToHoriz()`, the two
calendars and `DisplayRelation()`.

### Checked and found fine

- **`CastRelation()`** is as CONVENTIONS.md's ring-ownership section
  describes; the `rSav` capture is deliberately not a `Borrow` and its
  comment says why; the two GUI-only guards (`ciSave` reload for
  midpoint mode, `nRel` kept) are the fixes for measured recast bugs.
- **`PrintAspect()`** indexes `szNakshatra[]` by `(int)(pos/(360/27))+1`
  for a `pos` already reduced by `Mod()`, so 1..27; `rgchAppSep[]` is
  indexed `0..5`; the in-place `%` overwrite lands inside `%f` output.
- **`ChartInDayInfluence()` and `ChartTransitInfluence()`** bound their
  event arrays by `MAXINDAY` at every append; the sort comparators read
  only appended entries.
- **`ChartTransitInfluence()`** zeroes the natal velocities around the
  grid and restores them, swaps `ignore[]` for the transit cast and
  restores it, and recasts the main chart before returning.
- **`ChartCalendarMonth()`** clamps a negative month (a position file's
  `-1`) before indexing `szMonth[]`.
- **`DisplayRelation()`**'s `-rd` table formats through a `%%.%dlf`
  template built from `us.fSeconds`, bounded both ways.

### Findings

None.

---

## D3. `charts3.cpp` -- reviewed 2026-09-12

2,047 lines: the transit-to-transit search (`-d`) and its list printer,
the transit-to-natal search (`-t`), the text transit graph (`-B`/`-V`),
the rising/setting search (`-Zd`), the ephemeris (`-E`) and the
exoplanet transit list (`-Ux`).

### Checked and found fine

- **Every event array** (`id[]`, `ti[]`, `source[]`/`time[]`/...) is
  bounded at each append by `MAXINDAY`, and `ChartInDaySearch()`'s
  chunked output (`maxinday = MAXINDAY - (pid - id)`, the quarter-buffer
  shift) keeps the running pointer inside the array.
- **The interpolation divisors** (`MinDistance(cpA, cpB)`, `f2-f1`,
  `altA-altB`) are all behind a sign-change or inequality test that is
  false when the divisor is zero.
- **`ChartTransitGraph()`** frees every per-aspect slice array on its
  single exit label, including the eclipse arrays only when they were
  allocated; `cAsp <= cAspect` keeps the `[asp]` subscript inside
  `TransGraInfo`; the strength byte is `'0'..'9'` by construction.
- **`ChartTransitSearch()`'s two restriction swaps** are each done and
  undone symmetrically; the first covers `is.nObj` and the inner ones
  `oNorm`, which differ only for stars, and stars are never read as
  transiting bodies (`j <= oNorm`).
- **`ChartEphemeris()`** cannot divide by `us.nEphemFactor`, which `-E0`
  floors at 1.
- **`ChartExoplanet()`'s parser** assumes the quoted first field the
  shipped `astexo.csv` has (it drops one character at each end);
  `AdvancePast()` on a short line runs to the terminator and the
  `atoi`/`atof` calls then read zero. `cexod` from an empty file is -1,
  which `RgAllocate()` refuses.

### Findings

**D3-1 -- A zero period in the exoplanet table hangs the `-Ux` chart.**
*low, latent, upstream.*

    is.rgexod[j].JDLoop += pexod->period;      charts3.cpp, the loop

With `period == 0` the entry's `JDLoop` never advances: `(int)(x/0.0)`
is `INT_MIN` on x86, `INT_MIN * 0.0` is zero, so `JDLoop` stays at the
epoch, is selected as the minimum on every pass, and either `continue`s
forever (`jd < jd1`) or prints the same transit forever. The shipped
`astexo.csv` has no such row (4,544 rows, every period positive,
measured), so this is a hazard for an edited or replaced table. A
`period <= 0.0` skip at load time closes it.

**Status: fixed** — the loader now parses each row into a separate
write slot and drops entries whose period is zero or negative (the
name is only cloned for entries kept), with `is.cexod` counting what
survived. Reproduced first by running `-Ux` against a test copy of the
table with one row's period zeroed (the exe directory is searched
before the working directory, so the binary was run from the copy's
directory): baseline and pre-fix builds spin until killed (rc 124);
after the fix the run completes and the valid row beside it still
prints its transits (`-Uxm`). With the shipped table the fixed binary
is byte-identical to the baseline on `-Ux` and `-Uxm`, and the chart
and switch matrices are byte-identical throughout.

**D3-2 -- The event lists overwrite the chart the GUI's "Store Chart
Info" holds.** *note, upstream design; handed to the GUI reviews.*

`PrintInDays()`, `ChartTransitSearch()` and `ChartHorizonRising()` each
write `ciSave` with the chart of the event being printed. In the console
that is a feature: `-i set` afterwards recalls the last event
(`FInputData()`'s virtual-file table says so). In both GUI builds the
same variable is the Store/Recall slot (see A-1's comment in
astrolog.cpp), so after any `-d`, `-t` or `-Zd` chart, "Recall Chart
Info" hands back the last event's time rather than what the user
stored. Not measured live; recorded here from the three writers, for
the `qtdriver.cpp` and `wdriver.cpp` sections to confirm as
**Q-open-2** and **W-open-2**.

**Status: fixed** — Q-open-2 reproduced and fixed with the console
`-i set` feature untouched: the three event searchers
(`ChartInDaySearch`, `ChartHorizonRising`, and `ChartTransitSearch`'s
graphic-calendar branch, which the GUI reaches through the calendar's
event labels) save and restore `ciSave` under `WIN`/`QT` only. The
Windows build compiles clean with the same guard, closing W-open-2.
See the QD and QT sections for the falsified suite probe (Store a
chart, view a `-d` chart, Recall hands back the event) and the
`TestChartStoreQt` coverage that now holds it.

---

## IN. `intrpret.cpp` -- reviewed 2026-09-12

1,621 lines: the word-wrap accumulator `FieldWord()`, the general,
location, aspect, midpoint, Arabic-part, in-day, transit, synastry and
astrocartography interpretations, the esoteric interpretation and its
Ray-chart clues, and the influence computation (`-j`). REFACTORING.md D2
folded the nine rulership stanzas into `rgrulersys[]`; the "data-driven,
preserve" verdict stands. This pass found one thing the verdict did not
cover.

### Checked and found fine

- **`FieldWord()`'s static line** is `cchSzMax` (255) and it wraps at
  `us.nScreenWidth-1`, which `FValidScreen` caps at 200; a word longer
  than the width is cut at the width rather than overrunning.
- **`szSabian[]` subscripts** are `(int)planet` after `Mod()` (0..359)
  and `(n-1)*30 + (int)v` with `v` in 0..30.
- **`szModify[Min(nOrb, 2)][asp-1]`** stays inside `[3][cAspect]`.
- **`InterpretEsoteric()`'s `rgEsoObj[]` null guard** is real: the
  initializer stops well short of `oNorm1`.
- **`ComputeInfluence()`** reads object influence through `RObjInf()`,
  which clamps to `oNorm1` (the off-the-end read its comment records is
  fixed); the sign-power loop stops at `oNorm` for the same reason.
- **`SortRank()`** with `fObj` skips restricted slots consistently on
  both sides of its search.

### Findings

**IN-1 -- A user-supplied aspect interpretation is used as a `printf`
format string.** *high, upstream, every build.*

    sprintf2(S(sz), szInteract[asp], szModify[Min(nOrb, 2)][asp-1]);
                                                 intrpret.cpp:324, 483, 526, 629
    sprintf2(S(sz), szInteract[i], "");          intrpret.cpp:175

`szInteract[]` is whatever `-YIA <asp> <string>` set, from the command
line, a macro or a settings file (the writer emits customised entries),
and the default strings carry one `%s` for the modifier. Measured:

    -YIA 1 "reads %s then %s then %p then %s" -g -I
      Segmentation fault, exit 139 (plain build);
      ASan: SEGV in internal_strlen from printf_common
    -YIA 1 "x%n%n" -g -I
      *** %n in writable segment detected ***, exit 134

So a settings file can crash the program, and without fortify (the
MSVC build, or any `-O0` build) `%n` is a write to an attacker-chosen
address. Item 143 bounded the *destination* of every one of these
calls; the format argument was never part of that sweep, and this is
the only place in the program a user string reaches a format position
(checked: every other `sprintf2` in the interpretation code passes the
user text through `%s`).

*Fix shape:* stop treating the string as a format. Either substitute
the single `%s` by hand (find it, copy the two halves around the
modifier) or, cheaper, `sprintf2(S(sz), "%s", ...)` after a validation
at `-YIA` time that the text contains exactly one `%s` and no other
`%`, refusing anything else the way `FErrorValN` refuses a value.
*Net:* the two lines above, both exit 0 with the text printed
literally.

**Status: fixed** — the strings are user data and are never handed to
printf as a format: a local `InterpretFormatSz()` in intrpret.cpp
copies the string, gives the modifier to the first `%s`, prints `%%`
as one percent sign, and passes any other percent sequence through
literally. All five sites (the -HI list and the aspect, in-day,
transit and grid interpretations) route through it. Verified: the two
repros above exit 0 with the text printed literally (previously SEGV
and the `%n` abort), `100%%`/extra-`%s`/trailing-`%` edge cases print
literally, chart and switch matrices byte-identical against the
parent commit (default strings carry exactly one `%s`, so default
output is unchanged), suite 5191/0, mingw clean.

---

## EX. `express.cpp` -- reviewed 2026-09-12

2,945 lines: the AstroExpression interpreter -- the 570-plus-entry
function table `rgfun[]`, `FEvalFunction()`'s opcode switch, the
recursive parameter parser `PchGetParameter()`, the hand-built trie
(`CsCreateTrie()`/`ILookupTrie()`), and the growable variable/macro/
string stores with their accessors. REFACTORING.md Area G gave it a
largely clean verdict (a data-driven interpreter, table-driven where it
counts). This pass checked the function handlers' bounds one by one.

### Checked and found fine

- **Every astrology accessor guards its index**: `FValidObj`,
  `FNorm`, `FValidSign`, `FValidAspect`, `FValidRay`, `FSector`,
  `FRingObj`, `FBetween(n1, 0, cRing)` for the ring accessors,
  `FBetween(n1, 0, is.cci-1)` for the chart-list ones. `funObjInf`'s
  two-range split (`0..oNorm1`, then `oNorm1+1..oNorm1+5` into
  `rgrBonusInf`) is correct.
- **`funDiv`/`funMod`** guard a zero divisor; `funPow`/`funLn`/`funSqr`
  can produce inf/nan but that is the caller's arithmetic, returned as a
  value not an index.
- **`funChar`** checks `FBetween(n1, 0, cszExpStr-1)`, a non-null slot,
  and `n2 > CchSz(...)` before indexing the string.
- **`funVar`/`NExpGet`/`RExpGet`** check `< cparVar`; `FEnsureParVar()`,
  `FEnsureExpMacro()`, `FEnsureExpStr()` grow-or-fail before any write,
  and every assignment path (`funAssign`, `funFor`, `funMouse`, ...)
  goes through them first.
- **`CsCreateTrie()`/`ILookupTrie()`** bound the build stack by
  `cchSzMax` with an assert, cap substrings at 255, and check
  `csTrieMax` before every write; the 16000-short buffer holds the
  570-odd names with room (the header's ~2500-string limit).
- **`ExpSetString()`'s list split** and `ExpSetNums()` walk their input
  within the terminator; `PchFormatString()`/`PchFormatExpression()`
  take and honour a size.
- **The `%s`-format hazard is elsewhere:** every `sprintf2` here that
  takes a user string passes it through `%s` (the error and "unknown
  function" messages included). The one format-string defect is IN-1 in
  intrpret.cpp, not here.

### Findings

**EX-1 -- `Macro()`, `Switch()` and `Switch2()` test their index with an
inclusive upper bound, then use it as a subscript.** *low, upstream.*

    case funMacro:  if (FBetween(n1, 0, xi.cszExpMacro) &&
                        FSzSet(xi.rgszExpMacro[n1])) ...     express.cpp
    case funSwitch: n = is.rgszMacro != NULL &&
                        FBetween(n1, 0, is.cszMacro) &&
                        FSzSet(is.rgszMacro[n1]) && ...
    case funSwitch2: ... FBetween(n1, 0, xi.cszExpStr) &&
                        FSzSet(xi.rgszExpStr[n1]) && ...

`FBetween` is inclusive (`v >= v1 && v <= v2`), so `n1` equal to the
slot count reads one element past an array of exactly that many. Every
other slot accessor in the file uses `-1` on the bound (`funChar`,
`funMonL`, `funVar`, `ExpGetString`). The sibling `funVar` reads
`FBetween(n1, 0, xi.cparVar-1)`, which is the correct shape. I could not
make ASan fire on the exact boundary (the store rounds the small
allocation up, so the one-past pointer read can land inside the rounded
block), so this is by reading, not by a caught crash; the bound is still
wrong on its face and the fix is `-1` on all three, matching the rest of
the file.

### Handed to other files

Nothing new. IN-1 (the interpretation format-string crash) is the
express-adjacent finding worth cross-reading, but it lives in
intrpret.cpp.

---

## DA. `data.cpp` -- reviewed 2026-09-12

1,705 lines, almost entirely compile-time tables: names, abbreviations,
default orbs/angles/influences, the rulership/exaltation/ray tables,
object physical data, constellation names, the interpretation default
strings, and the palette. The fork's changes against upstream are
mechanical: Placalc removed from `szEphem[]` and the backend seed
tables, the flat arrays retyped to the checked/guarded families
(`TBLOBJ`, `TBLSIG`, `TBLASPR`, `GRDOBJ*`), and `rObjOrb[]`/`rObjAdd[]`
folded into `rgobjset[]` (the `OBJSET` refactor). No logic.

### Checked and found fine

- **`tools/defaults_audit.py` is clean** on both legs (initializer
  counts and values against `astrolog.as`), which is the net for this
  file; the `rulership`, `esoteric-tables` and `oracle` suite groups
  cover the table *values*.
- **Declared sizes match initializer counts** on the hand-maintained
  string tables spot-checked (`szObjName[objMax+4]`, `szMindPartDef
  [objMax]`, `szCnstlName[cCnstl+1]`, `ai[cPart]`, `szColor[cColor2+5]`).
- The `szColor[]` five extra trailing names (`Element`, `Ray`, `Star`,
  `Planet`, `Auto`) past `cColor2` are what `NParseSz(pmColor)` and
  `SzColor()` rely on; A-2's `kPlanet` case is the one consumer that
  can index `kObjA[]` with the result out of range, and that is an
  `InitColors()` bug, not a table one.

### Findings

None. The one table this fork owns rather than inherits, `rgObjSel[]`,
lives in calc.cpp and is covered by CLAUDE.md's ephemeris-list rule and
`TestObjSelTableQt()`.

---

## AT. `atlas.cpp` -- reviewed 2026-09-12

2,155 lines: the atlas and timezone-database loaders
(`FLoadAtlas`/`FLoadZoneRules`/`FLoadZoneChanges`/`FLoadZoneLinks`), the
city lookup (`DisplayAtlasLookup`/`DisplayAtlasNearby`), `AdjustTime()`,
and the timezone-change engine (`DisplayTimezoneChanges`/`FSetDstZon`/
`ZondefFromIzn`). REFACTORING.md B1/B3/G1/G2 covered the loader family
and the `pfnAtlasRow` sink; this pass checked the bounds.

### Checked and found fine

- **`DisplayAtlasLookup`/`DisplayAtlasNearby`** bound the `szCity` copy
  (the `-zN` stack-smash fix, re-confirmed: a 260-character city is
  truncated and misses cleanly under ASan), and their insert-sort caps
  the shift index at `ilistHi-1 <= ilistMax-1`. The match-power read
  `szNam[j + CchSz(szCity)]` lands on the match's trailing character or
  the NUL, inside the array.
- **Every loader** checks the per-record `fgets`, validates months and
  weekdays through `NParseSz`, checks `cchSzZon` before copying a rule
  name, and checks its running entry total against the caller's max
  before writing (`prun[1].irue > irueMax`, `rgizcChange[i+1] >
  izceMax`).
- **The `Borrow` conversions** in the two display functions fixed the
  two settings `DisplayAtlasNearby` leaked on its early return (work log
  item 175); confirmed against the code.
- **`AdjustTime()`** terminates: each `while` moves a value strictly
  toward its range.

### Findings

**AT-1 -- The zone-change and zone-rule loaders index `iznMax`-sized
globals by counts the file declares, with no clamp.** *low, upstream,
trusted-data.*

`FLoadZoneChanges()` writes `rgznChange[i]` for `i` up to `izcnMax-1`,
and `FLoadZoneRules()` sets `is.crun = irunMax`, which
`FLoadZoneChanges()` then uses to index `rgfUsed[iznMax]` and its final
`for (i = 0; i < is.crun; ...)` check. `rgznChange[]` is `[iznMax]`,
`rgfUsed[]` is `[iznMax]` (429), but `izcnMax`/`irunMax` come from the
`-YY1`/`-YY2` argument counts with no bound against `iznMax`. The
shipped `timezone.as` declares 142 rules and 417 zones, both under 429,
so nothing overflows in practice; a hand-crafted timezone file with
duplicate zone names or more than 429 rules would write past those two
globals. Same trust boundary as the ephemeris files (a corrupted
bundled data file); recorded because the arrays' dimension and the loop
bound come from unrelated sources. A clamp (`if (izcnMax > iznMax)
error`) at the top of each loader closes it.

### Handed to other files

Nothing new. `DisplayAtlasLookup`'s `size_t`-to-`flag` parameter (G1)
was already discharged in the survey; the Qt and Windows callers pass
the `flag` form.

---

## XG. `xgeneral.cpp` -- reviewed 2026-09-12

2,346 lines: the drawing primitives every chart is built from -- pixel,
block, line, dash, arc, ellipse, crescent, flood fill, text, the glyph
and turtle-graphics drawers. Each multiplexes the screen backend
(X11/Win/Qt, compile-time) and the file format (`gs.ft`, runtime).
REFACTORING.md E1 covered the target structure; the nets here are
`graphics-matrix.sh` (224 renders checksummed), `image_audit.py`
(output-file validity), the Qt suite's `screen-options` group, and
`ubsan-sweep.sh` (which found this file's two subtle out-of-bounds:
the metafile 8-into-4-byte store and the `space[]` over-read).

### Checked and found fine

- **`DrawPoint()`** returns on `!FOnWin(x, y)` before any buffer write,
  and its thick-pixel neighbours are each guarded (`x+1 < gs.xWin`,
  `y+1 < gs.yWin`); it is the base every higher primitive funnels
  through.
- **`DrawFill()`** bounds its Qt flood by the image rectangle, not by
  `FOnWin()` (the 538-warnings-per-run fix its comment records), and
  its fixed `iFillMax` queue wraps rather than overruns; the "queue
  full, skip" guard is commit ee's measured constellation-BFS answer.
- **`DrawTurtle()`/`NFromPch()`** walk a NUL-terminated command string,
  reject a bad command with a bounded error, and draw through
  `DrawLine()` which clips.
- **`DrawSz()`** converts to Latin through a sized `sz2[cchSzMax]`
  buffer (`ConvertSzToLatin` takes the size); `DrawSign`/`DrawObject`/
  `DrawNakshatra` use `sz[3]` for a one-glyph string.
- The pixel accessors (`_PbXY` and friends) carry no bound of their
  own; every caller reaches them past an `FOnWin()` or image-rectangle
  guard, which is the file's contract.

### Findings

None new. The two out-of-bounds this file once had are fixed and pinned
by the UBSan sweep.

---

## XD. `xdevice.cpp` -- reviewed 2026-09-12

3,161 lines: the bitmap structure and its accessors, `FAllocateBmp`,
the background-map compositing, the file-format writers (BMP, PNG, XBM,
Ascii, PostScript, Windows metafile, SVG) and the wireframe/globe
renderer. `image_audit.py` validates every writer's output as a real
file of its format (BMP size field and row padding, every PNG chunk CRC
and an IDAT that inflates to exactly `h*(w*3+1)`, XBM element count,
PostScript and SVG structure); this pass read the writers for the
bounds that audit cannot see from outside.

### Checked and found fine

- **`FAllocateBmp()`** refuses a bitmap over `zColmap` in either
  dimension and checks `CbColmap()` for overflow before allocating.
- **`WriteXBitmap()`** takes the filename's last component into a sized
  buffer (the 95-into-80 overflow, work log item 134), and its
  column-group loop tests `x+i < gs.xWin` *before* fetching the pixel
  (the `^`-precedence fix its comment records).
- **`WriteBmp()`/`WritePNG()`/`WritePNGChunk()`** compute their headers
  and CRCs correctly (the audit confirms from the output side); the
  `Assert(ib == ...)` in `WritePNG` pins the buffer fill against its
  allocation.
- **`WritePNG`'s `cRowBlock = 65535 / cbRow` cannot divide by zero.**
  `cbRow` is `b->x*3+1`, and `b->x` is bounded by `BITMAPX` (4096), so
  `cbRow` maxes at 12,289 and `cRowBlock` is at least 5. A window wider
  than ~21,845 would make `cRowBlock` zero and the next line
  (`cBlock = (b->y + cRowBlock-1) / cRowBlock`) a division by zero, but
  `FValidGraphX` caps the width far below that. Recorded because the
  divisor is unguarded and the only thing between it and a `SIGFPE` is
  the window-size limit.
- **`BeginFileX()`/`EndFileX()`** open with the right binary/text mode
  per format and guard `gi.file == NULL`.
- **The wireframe and globe math** (`WireDrawGlobe` and kin) is the same
  projection code the sphere charts use, exercised by the graphics
  matrix; no unguarded index math found in the write path.

### Findings

None new.

### Handed to other files

- **XD-note** (for whoever revisits `WritePNG`): if `BITMAPX` is ever
  raised past ~21,845, add a `cbRow <= 65535` guard before the
  `65535 / cbRow` division. Not a live bug at 4096.

  **Status: fixed** — the guard is in place now rather than waiting for
  the raise: `WritePNG` asserts `cbRow <= 65535` before the division
  (live in the DEBUG/test build, compiled out in production), so a
  future `BITMAPX` increase fails the suite's render groups instead of
  dividing by zero. Verified by reading plus a clean mingw build.

---

## XS. `xscreen.cpp` -- reviewed 2026-09-12

1,759 lines: the colour-palette setup (`InitColorPalette`/`InitColorsX`),
the window and event lifecycle for X11/WCLI (`BeginX`, `InteractX`,
`EndX`, `WndProcWCLI`), animation, `CommandLineX`, window sizing, the
`-YXU` star-link parser, `DetectGraphicsChartMode`, and `FActionX` --
the entry every graphics chart passes through. The `-X`/`-YX` switch
*parsers* moved to switch.cpp's registry (reviewed as section S); what
remains here is lifecycle and dispatch, netted by `graphics-matrix.sh`
and the Qt suite.

### Checked and found fine

- **`FActionX()` clamps `gs.xWin`/`gs.yWin` to `BITMAPX`/`BITMAPY`
  before allocating** the bitmap, which is what keeps XD's `WritePNG`
  divisor safe; the map-fit and grid-autoscale size math is idempotent
  on redraw (the comments prove the fixed points).
- **`DetectGraphicsChartMode()`** scans the shared `rgchartmode[]` table
  (work log item 111), keeping the two documented specials (`-HA` at
  `-g`'s slot, `rcBiorhythm` a value test); it is one of the four
  consumers A3 unified.
- **`InitColorsX()`** builds every mono/colour array through the checked
  aspect/ray tags (`kAspB[ASPT(i)]`, `kRayB[RAYT(i)]`), the `cRay+1`
  aggregate slot included.
- **`FProcessYXU()`** sizes each allocation from the actual string
  lengths before formatting into it (the dangling-pair fix, commit
  e491870).
- **`CommandLineX()`** shares the 255-byte prompt buffer flagged in A-5;
  no new issue.

### Findings

None new.

---

## XT. `xdata.cpp` -- reviewed 2026-09-12

1,108 lines, entirely graphics data: the vector-font glyph tables, the
default draw-object and draw-aspect strings, the coastline/constellation
turtle strings, and the graphics globals. No functions. REFACTORING.md
Area E's verdict ("it's data, leave it") holds; the glyph and coastline
strings are consumed by `DrawTurtle()`/`DrawGlyph()`, which the graphics
matrix exercises, and any bad element count would surface as a garbled
render diffed against the pinned checksums.

### Findings

None.

---

## XC. `xcharts0.cpp`, `xcharts1.cpp`, `xcharts2.cpp` -- reviewed 2026-09-12

7,280 lines together: the graphics chart renderers -- the sidebar and
info block, the wheel machinery, the aspect/midpoint grids, the horizon
and telescope and sphere and globe and map charts, astrocartography, the
ephemeris and transit graphs, and `DrawChartX()` (the graphics mode
dispatch). This is target-free code sitting on the xgeneral primitives;
its nets are `graphics-matrix.sh` (224 pinned-time renders checksummed),
`inert_option_audit.py` (every file-render option moves a render), the
Qt suite's `screen-options` group (the screen ones), and
`ubsan-sweep.sh` run over the graphics matrix. REFACTORING.md Area F
(F1-F4) merged the projection-helper triplicates, split `DrawPrint`'s
three calling conventions, and recorded `DrawChartX` as the fifth mode
knower A3 unified.

### Checked and found fine

- **No unbounded `sprintf`** in any of the three files; every format
  goes through `sprintf2`/`S()`, and the sidebar/info buffers are
  `cchSzDef`/`cchSzMax`.
- **The parallel `real` arrays** (`xsign`, `xhouse`, `xplanet`,
  `symbol`, and the `ObjDraw rgod[objMax]` sets) are all sized `objMax`
  or `oNorm1` and filled by loops bounded by `is.nObj` (<= `cObj` <
  `objMax`) or `oNorm` -- the flat-array storage rule (work log item
  63), not a finding. `rOrb`/`rDiff[MAXINDAY]` in the transit graph
  match the text version's bound.
- **`DrawPrint()`** stops at `yPrint >= gs.yWin-1` and only draws;
  `DrawPrintTo`/`DrawPrintShift` own the cursor (F3 fix). `DrawZodiac`'s
  `pch[14] = chNull` lands inside `SzZodiac`'s `szZod[32]`.
- **`DrawChartX()`'s dispatch** carries the `WIN||QT` fall-throughs for
  the transit-list modes (`gTraTraTim`/`gTraTraInf`/`gTraNatTim`/
  `gTraNatInf`), the fix its comment records for the GUI drawing a bare
  frame; the globe-wire fall-through into polar is deliberate.
- **The final info line** builds through `S(sz)` with `us.fAnsiChar`
  borrowed and restored around it.

### Findings

None new. Anything out of bounds in a rendered chart would surface
either as a checksum move in the graphics matrix or as a report in the
UBSan sweep over it; neither shows one, and the read did not find an
index or format that escapes the object-count bounds.

---

## QD. `qtdriver.cpp` -- reviewed 2026-09-12

6,057 lines, the fork's own code: the main window and canvas, the menu
bar (built from `astrolog.rc` via the generated tables), `RedrawQt`
(the screen render path), the `-W` switch handler `NProcessSwitchesQt`,
animation, the theme/font machinery and desktop dark-mode detection,
printing and clipboard, and the AstroExpression Windows-function
accessors. The suite covers most of it directly: 25 dialogs, 42 context
menus, 264 shortcuts, the chart-mode table, the desktop light/dark
detection from each source, and the interface-settings round trip.

### Checked and found fine

- **`RedrawQt()` brackets `Action()` with an `is.S` save/restore**
  (`fileSav = is.S; is.S = stdout; Action(); ... is.S = fileSav`), so
  A-1's dangling `-os` handle is overwritten before anything else reads
  it. **Q-open-1 answered: the Qt build is not exposed to A-1.** With
  `-os` set from a settings file the text chart goes to the file rather
  than the canvas each redraw, a behaviour oddity but not a crash.
- **`RedrawQt`'s two-pass text sizing** bounds the enlarged buffer by
  `BITMAPX`/`BITMAPY` and uses `FPrintTables()`+`PrintChart()` on the
  second pass (not `Action()`), the fix its comment records for the
  Help tables and the double `-~Q` hooks.
- **`NProcessSwitchesQt()`'s `-W` arities match** the other two
  consumers (`NProcessSwitchesNullW` in switch.cpp, `NProcessSwitchesW`
  in wdriver.cpp), which the `interface-settings` group asserts on one
  line; `-WF`/`-WG`/`-Wx`/`-WI` all range-check and refuse rather than
  clamp; the `=WFa`/`=WGa` ternary-assignment trap is avoided as its
  comment notes.
- **`RedrawQt` enforces `-0X`** (`us.fNoGraphics`) and the jet-trail
  and reverse/mono derivations, each a documented fix for a menu item
  that did nothing on screen.
- **`AnimTickQt`'s `s_fAnimTickQt` guard** prevents a nested cast
  re-entering the frame timer.

### Findings

**Q-open-2 -- The `-d`/`-Zd` event printers overwrite `ciSave`, which
is the GUI's Store/Recall slot.** *low-medium, upstream, plausible from
reading; not reproduced.*

`PrintInDays()` (charts3.cpp:176) does `ciSave = ciCast` per event with
no restore in `ChartInDaySearch()`, and `ChartHorizonRising()` writes
`ciSave = ciMain`. In the console this is the `-i set` feature; in the
Qt build `ciSave` is the Store Chart Info slot (`ciSave = ciMain` at
qtdriver.cpp:3566, recalled at 3569). So viewing a `-d` or `-Zd` chart
and *then* using Recall should hand back the last event rather than the
stored chart. I could not demonstrate it from a command line, because
every switch (including the `-i set` recall) is parsed before `Action()`
runs the search, so the ordering that would show it only occurs
interactively (view chart, then click Recall). Recorded from the code,
with the reproduction being a GUI probe: Store a chart, view a `-d`
chart, Recall, check the date. `ChartTransitSearch`'s own `ciSave` write
(charts3.cpp:1008) is inside the graphic-calendar branch and does not
fire for a text `-t`, which is why `-t` is not affected.

*Fix shape:* if this reproduces, save and restore `ciSave` around
`ChartInDaySearch`/`ChartHorizonRising` in the GUI builds, the same
guard shape the midpoint recast already uses.

**Status: fixed** — it reproduces. A scratch probe measured `ciSave`
move from the stored chart to 9/12 22:12 after a text `-d` render and
again after `-Zd`; the two legs added at the end of `TestChartStoreQt`
(store a 1990 chart, `SetChartModeQt(gTraTraTim)` or `gRising` in text
mode, Recall) fail `yea 2000` — the event chart's year — against the
unguarded printers, and pass with the fix. The fix is the fix shape's
guard, `CI ciSaveSav = ciSave` on entry and `ciSave = ciSaveSav` on
exit, around `ChartInDaySearch`, `ChartHorizonRising` and
`ChartTransitSearch` under `#if defined(WIN) || defined(QT)` — the
third writer too, since the graphic calendar reaches the same write
through the GUI. The console builds don't compile the guard, so `-i
set` keeps the last event. Proven to fail without the fix: with the
three restores removed by hand the same two checks fail again. The
`WIN` half also closes the Windows half of D3-2 (wdriver's Store/Recall
reads the same slot).

### Handed to other files

- Still open for the `qtdialog.cpp` pass: the Store/Recall command
  handlers (qtdriver.cpp:3566/3569) and `SetRelQt`'s midpoint stash
  both use `ciSave`; confirm the dialog's own `ciSave` write
  (qtdialog.cpp:2294) does not collide.

---

## QI. `qtdialog.cpp` -- reviewed 2026-09-12

5,308 lines, the fork's own code: all 25 dialogs, transcribed from
`astrolog.rc` through the generated `qtrcdlg.h` table and the dialog-unit
layout engine (`RcBuildDialogQt`), plus the field marshalling, the atlas
lookup UI, the chart-list and chart-info dialogs, and the fork's Object
Selections dialog. This file has the densest net in the project: seven
`rc_*_audit.py` tools (controls wired to nothing, wrong setting, wrong
polarity, wrong control kind, wrong cast class, by-name lookup
resolution), `backend_parity_audit.py`, `settings_coverage_audit.py`,
and the suite's 25-dialog open/close, field-validation, and
Object-Selections groups.

### Checked and found fine

- **`SzFieldQt()` writes at most `cchSzMax` into its destination**, and
  every caller passes an `sz[cchSzMax]` buffer (checked at 1541, 1705,
  2084 and the rest) -- the hardcoded bound matches every call site, so
  the "don't `S()` a parameter" trap is avoided by a fixed size rather
  than `sizeof`.
- **No unbounded `sprintf`** anywhere in the file; the field parsers
  (`NFieldQt`/`RFieldQt`) route through `NFromSz`/`RFromSz` on a
  `toLocal8Bit` copy.
- **`SzObjNameQt()` bounds its index** (`FBetween(obj, 0, cObj)`), the
  guard its comment describes for settings arriving before validation.
- **The colour combos** go through `NColorFromComboQt` →
  `NParseSz(pmColor)` with `FValidColorMA`/`FValidColor2A` checks (the
  same validators reviewed in the io/general pass); the object-def and
  midpoint parsing is the shared `FObjSelParse`/`SzObjSelName` code from
  calc.cpp (section C).
- **The fixed control arrays** (`rgpcbFont[cFontEntry]`,
  `rgpcb`/`rgignore` sized by object range, `rgod[cObjSelRow]` and its
  siblings) are each filled by loops over their own bound.

### Findings

None new.

### Handed to other files

- **The `ciSave` collision (Q-open-2) is not made worse here.** The
  chart-info dialog's only `ciSave` reference (qtdialog.cpp:2294) is the
  "Set" button *reading* the Store slot into the dialog fields; the
  Store slot is written only in qtdriver.cpp:3566. No dialog writes
  `ciSave`, so the open question stays exactly as stated under QD: the
  `-d`/`-Zd` printers, not any dialog, are what could clobber it.

---

## QT. `qttest.cpp` -- reviewed 2026-09-12

13,082 lines, the in-process test suite: 78 groups covering the dialogs,
menus, shortcuts, chart renders, the settings sweeps, the numeric
oracle, and the shared-core fixes. This is the net the rest of the
project is trusted against, so the review question is soundness -- would
a group fail without the fix it guards, and is any group itself the
"regression test that is the regression" hazard CLAUDE.md records.

### Checked and found fine

- **Global state is saved and restored per group.** The groups sampled
  (`TestChartStoreQt`, the render and text groups) capture `ciMain`/
  `ciTwin`/`ciCore`/`ciSave`/`us.fGraphics`/`us.nRel`/`gi.nMode` and put
  them back; new code uses the `Borrow` guard.
- **All 24 `is.S` assignments are paired** `fileSav = is.S; ...;
  is.S = fileSav`, and the groups that drive code which `PrintSz`es
  (transit, atlas, interpretation, the UTF-8 cell-count test at 12289)
  set `is.S = stdout` or a real file first -- the item-165 stale-`FILE`
  trap is handled explicitly, with the comment at 12270 naming it.
- **No direct `PrintChart`/`PrintSz` on an unopened stream**; the one
  bare `PrintSz` (the charset cell-count check) brackets itself with
  `is.S = stdout` and restores.
- **`nScaleTest` multiplies every timer by 10 under the test build**,
  which is why the ASan run is slow but not racy (CLAUDE.md's 3600s
  watchdog note).

### Findings

**QT-1 -- `TestChartStoreQt` does not cover the `-d`/`-Zd` event-search
path, which is exactly Q-open-2's gap.** *coverage, not a test defect.*

The Store/Recall group proves `ciSave` survives a text redraw, a text
capture (Save/Copy/print), the Chart Info dialog OK, and the midpoint
stash -- the four paths the fork already fixed. It does **not** store a
chart, redraw a `-d` or `-Zd` chart, and recall, which is the one path
where `PrintInDays()`/`ChartHorizonRising()` write `ciSave` with no
restore (Q-open-2). So if that path does clobber the Store slot
interactively, this suite would not catch it. The fix for Q-open-2
should add that case here: Store chart A, `SetChartModeQt` to a text
`-d`, `RedrawQt`, Recall, `Check(ciMain.yea == A)`. Falsify it by
confirming it fails against the current printers before guarding them.

**Status: fixed** — the two legs are in `TestChartStoreQt`, after the
midpoint leg, which is where they belong: placed before it they left
`us.fHorizonSearch` active, the midpoint leg's own text redraws ran
`ChartHorizonRising` inline, and the per-event `ciSave` writes walked
the midpoint — the shared-slot collision again, arriving through the
test's own ordering. They reproduce Q-open-2 against the unguarded
printers (8 passed, 2 failed in the group) and pass with the guards
(10 passed, 0 failed).

### Handed to other files

Nothing. This is the last C/C++ source file in the fork; the review is
complete.

---

## Summary — findings across the review

The whole fork's C/C++ source is reviewed: `astrolog.cpp`, `switch.cpp`,
`io.cpp`, `general.cpp`, `calc.cpp`, `matrix.cpp`, `charts0-3.cpp`,
`intrpret.cpp`, `express.cpp`, `data.cpp`, `atlas.cpp`, the seven
`x*.cpp` graphics files, and the three Qt port files. The vendored Swiss
Ephemeris (`swe*.cpp`) was out of scope. The suite passes 5191/0 as of
this review, 5193/0 after the campaign (two `TestChartStoreQt`
assertions added).

**Every finding is resolved.** Each entry above carries its evidence
and, under its **Status** line, what was done and how it was verified.
Of the 37 numbered findings plus the three cross-file opens and the
XD-note:

- **Fixed (23):** IN-1, A-1, A-2, A-3, A-4, A-5, A-11, S-1, S-2, S-7,
  S-8, I-1, I-2, I-4, I-5, I-6, G-1, G-2, G-5, C-1, C-2, C-3, EX-1,
  AT-1, D0-1, D0-2, D1-1, D1-2, D3-1, Q-open-2/QT-1/D3-2 (one guard
  story), A-6, A-7, XD-note.
- **Closed without a change (9), each with its reason on the finding:**
  A-9 (unreachable), A-10 (the second `</font>` is usually
  load-bearing), S-4 (the double message is the honest cost of
  "consume nothing on failure"), S-5 (upstream's documented contract),
  S-6 (no victim in any file that exists), G-3 (NULL is outside the
  comparator's contract), G-4 (no caller; the buffer grows when the
  first arrives), I-3 (a pattern hazard for new readers).

The cross-cutting observations stand: the highest-consequence items
(IN-1, A-1, I-1, A-2, G-2, C-2) were all upstream code the fork
inherited, so their fixes also fixed the Windows product built from
this same core, and each was checked against `Makefile.win`. Several
were invisible to the existing nets for a structural reason worth
keeping: a freed `FILE *` (A-1) and a format-string `%n` (IN-1) are
both outside what AddressSanitizer instruments, the `-0q` spin (A-3)
made the ASan build time out before it could report, and the
star-sort trigger paths (D0-1/D0-2/D1-1) exist in no matrix leg, so
their nets were targeted before/after diffs. Two review claims were
corrected in passing: A-3's cycle lives in `NInputRange`'s reprompt
loop, not `main()`'s (so the first fix shape could not have worked),
and EX-1's one-past read *does* fire under ASan when the probe uses
the lists' grow quantum.
