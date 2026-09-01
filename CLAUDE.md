# Astrolog — Qt/Linux GUI port

This fork adds a **Qt GUI backend for Linux** to Astrolog 8.00, with the
same menus and dialogs the Windows build has. The goal, stated plainly:
*a Windows user should feel like they're running the same app, just on
Qt/Linux.* Parity with Windows is the spec — when in doubt, do what
`wdriver.cpp`/`wdialog.cpp`/`astrolog.rc` do.

Work happens on branch **`qt`**.

## Orientation

- **`README.md`** — what the fork is, for someone who isn't working on it.
- **`QT_GUI_PLAN.md`** — the working document. Architecture, gotchas,
  per-menu status, "What to do next", a work log of every item and what it
  actually turned out to be, and every knowing divergence from Windows.
  **Read this before changing anything.**
- **`QT_TESTING.md`** — **read this before testing anything.** The fast
  loop is a scratch probe inside `qttest.cpp` (`ASTROLOG_QT_PROBE=1`,
  ~0.2s a question, no display at all), not driving a window. Also the
  failure modes that waste hours; most of what looks like a rendering
  problem or a hang is in there.
- **`QT_COMPARING_WITH_WINDOWS.md`** — how to build and drive the real
  Windows binary under Wine and diff it against this port, and the
  headless-automation traps specific to doing that.
- **`QT_MENU_MAPPING.md`** — the Windows menu structure extracted from
  `astrolog.rc`, with command IDs.
- **`REFACTORING.md`** — the standing architectural review: what makes
  the codebase hard to evolve, with evidence, and the survey ledger that
  says which area to review next. Read it before any refactoring work.
- **`CONVENTIONS.md`** — the codebase's actual conventions, verified
  and written down: naming, none-values, macro families, buffer and
  error idioms, how to add a command. Read it before writing new code.

The port lives in `qtdriver.cpp` (window, canvas, menus) and
`qtdialog.cpp` (dialogs), selected with `-DQT`, standing in for the
Windows-only `wdriver.cpp`/`wdialog.cpp`. The whole command-line switch
surface lives in `switch.cpp` (see REFACTORING.md, "The registry as
built").

Two different kinds of change reach the shared core, and the difference
matters when merging a new upstream release:

- **Porting work** touches it only where a backend branch is genuinely
  missing, always inside `#ifdef QT` — or, where Windows already has the
  branch and this port wants the same, by widening it to
  `#if defined(WIN) || defined(QT)`. Both forms count, so the sweep is
  `grep -lnE "ifdef QT|defined\(QT\)" *.cpp *.h`. A `WIN`-only branch
  with no `QT` in it is the shape bugs hide in: see work log items 39
  and 54, one of which was a plain value in a struct initialiser.
- **Upstream merges ended at work log item 63** (2026-08-29), by the
  maintainer's explicit decision: the per-object settings storage is
  `rgobjset[]`/`OBJSET` now, and upstream's ~130 references to the five
  flat arrays it replaced no longer apply. The Windows *build* is
  unaffected — `Makefile.win` compiles this same core and remains the
  behavioural oracle.
- **Features this fork adds to *both* builds** — the Object Selections
  dialog and the settings-save fixes — deliberately carry no `QT` guard
  at all, because they go into `wdialog.cpp`, `astrolog.rc`, `io.cpp` and
  `calc.cpp` as upstream would take them. `calc.cpp` has zero `ifdef QT`
  in it. See "Features this fork adds to both builds" in
  `QT_GUI_PLAN.md`.

## Prerequisites

```sh
sudo apt install qtbase5-dev pkg-config              # build the Qt port
                                                    # (Qt Network too, same package)
sudo apt install g++-mingw-w64-x86-64 wine           # build and run the Windows one
sudo apt install xvfb metacity xdotool imagemagick   # drive either headlessly
sudo apt install python3-pil                         # compare captures
```

Only the first line is needed to build the port and run its whole test
suite. The rest is for comparing against Windows.

### On a fresh clone, before anything else

Three things live in `.git/config` or on the machine, so none of them
survive a clone and all three have bitten:

```sh
git remote -v                                    # upstream push must read
git remote set-url --push upstream DISABLED      # DISABLED; set it yourself
git config user.name  nrvate                     # or commits land under
git config user.email nrvate@gmail.com           # the wrong author
```

And **`/swe` is a machine-local ephemeris mount, not part of this repo** —
887,000-odd files, kept on the NAS. `nrvate.as` points `-Yi1`/`-Yi2`/
`-Yi3` at it. Without it the main planets still compute (the bundled
`ephem/` and the Moshier formulas cover them — the Sun measured 0.04
arcsec off, 24Gem07'46.399" against 46.440")
but **every esoteric body reads `0Ari00'00"`** — measured, not assumed —
so anything touching the 39 esoteric bodies quietly tests nothing. That
is the whole reason for the `-i nrvate.as` rule below.

## Build and test

```sh
make qt -j4                      # ./astrolog-qt
make qt-test -j4                 # ./astrolog-qt-test
make all -j4                     # all four, and "make clean-all" undoes it
                                 # (plain "make" is upstream's target and
                                 # still builds only ./astrolog, which ten
                                 # scripts here depend on)
./run-qt-tests.sh                # 3526 assertions + startup checks
ASTROLOG_QT_TESTS=animation ./run-qt-tests.sh   # just one group, <1s
                                 # (=list names them; see QT_TESTING.md)
```

`run-qt-tests.sh` is headless — no X display needed. Run it before every
commit. Current state: **3526 passed, 0 failed**, startup diagnostics ok. The full suite is also clean under AddressSanitizer (`make -f Makefile.qt.asan`) — but note that
build is `-O0`, where `_FORTIFY_SOURCE` is inactive, so it structurally
cannot see a fortify-detected overflow. Work log item 142 was invisible
to it for that reason and had to be caught in an optimized `-g` build.

What it covers: 25 dialogs open/close with the right titles, 42 context
menus resolve, 264 shortcuts bound and unique, 26 chart types render
non-blank, all 35 text chart modes survive 120-character chart names
and locations, the five import file formats parse and their long-line
truncation points hold, all 338 menu items fire without crashing, 258/258 Windows menu
items present, 256 show Windows' own accelerator text, 39/39 esoteric
bodies resolve against the ephemeris, and bad input (missing files,
unknown switches) doesn't terminate the process.

One group is not like the others. **The numeric oracle** (`oracle`, 307
assertions) is the only net here that can say a number is *right* rather
than *unchanged*: it asks the Swiss Ephemeris library the same question
Astrolog asks it, through an object mapping transcribed independently in
`qttest.cpp`, and requires the same answer -- exactly, measured at
0.000000 arcsec over 15 bodies and 7 epochs. It also cross-checks
Astrolog's own Matrix formulas against Swiss, and requires all 40 house
systems to partition the circle once. Every other harness in this project
is differential and cannot distinguish "correct" from "unchanged"; see
work log item 141, and items 140 and 142 for the two shared-core bugs it
found — one before a line of it was written, one a fortify abort that had
been hunted twice and left open.

Several groups drive real dialogs rather than calling into them: Object
Selections through seven cases, the Calculation Settings ephemeris list,
and the chart list's filter. Others cover behaviour no audit can see --
relationship chart modes surviving a recast, the AstroExpression hooks
and functions, the desktop light/dark detection read from each of its
sources, and a check that queued timers fire inside nested modals, which
every dialog test depends on. A separate **Startup diagnostics**
section runs
the binary as its own process, because an in-process suite cannot test
the startup that happens before its own event loop (see plan item 27).

Eight standing audits, all currently clean — four of the port against
`astrolog.rc`, one of the compiled defaults against `astrolog.as`, and
one of the switch registry against the help text and settings writer:

```sh
python3 tools/rc_audit.py            # dialog controls nothing wires up
python3 tools/rc_mnemonic_audit.py   # "&" placement, all 850 label sites
python3 tools/rc_field_audit.py      # a control wired to the *wrong* setting
python3 tools/rc_lookup_audit.py     # a by-name lookup that resolves to no
                                     # control, or more than one, in the
                                     # dialog using it -- the layer below
                                     # rc_field_audit, which reads the table
                                     # as text and cannot see what got bound.
                                     # Per dialog on purpose: qtrcdlg.h holds
                                     # all 24 concatenated and the symbols
                                     # recur, so a table-wide check passes on
                                     # nearly anything
python3 tools/defaults_audit.py      # data.cpp initializer counts and
                                     # values vs astrolog.as, incl. the
                                     # known-preference allowlist; found
                                     # ruler2[] one short on its first run.
                                     # With a file argument: count leg only,
                                     # for any .as
python3 tools/fixture_coverage_audit.py  # every ranged settings switch
                                     # has a round-trip fixture line;
                                     # closes half of item 140's class
python3 tools/line_endings_audit.py  # any carriage return in a tracked
                                     # text file; the tree is LF
                                     # everywhere since work log item 159
python3 tools/registry_audit.py      # every spelling the -H text documents
                                     # or FOutputSettings() writes resolves
                                     # to a registry row; found -YYI dead
                                     # behind a misspelled ifdef on its
                                     # first run
```

And a seventh that is not fast and not resource-shaped: **the compiler
itself**, which nothing here read until 2026-09-01.

```sh
tools/warning_audit.py               # all four builds clean with -Wall,
                                     # every warning diffed against
                                     # tools/warnings.txt (316 in 100
                                     # sites); ~6 minutes, so pre-release
                                     # rather than pre-commit
tools/warning_audit.py --file io.cpp # one file, seconds, no baseline --
                                     # the loop to use while fixing
```

It earns its place the way the sanitizer sweeps did. Its first run found
that **`make -f Makefile.win` had not compiled since 2026-08-29**, 62
commits, while three work log items listed "Windows builds" among their
nets -- the oracle build was dead and the error was `Error 1`, which the
old checks did not match. Warnings are a real signal here for a second
reason: item 143's sweep left GCC naming every buffer still too small for
its worst case, and two of those were live overflows (work log items
146-147).

And four **differential harnesses**, which are a different kind of check:
each prints a normalized behavioural artifact for one binary, so two
builds of this tree can be byte-diffed and an empty diff is the proof.
Not pre-commit — each needs a baseline binary built from the commit you
are changing (`git worktree add`; the recipe is in QT_TESTING.md).

```sh
tools/chart-matrix.sh <binary>       # 71 invocations: every text chart the
                                     # console build draws over a pinned
                                     # date, plus seconds/sidereal/3D
                                     # variants, transits, progressions,
                                     # relationship charts and the
                                     # interpretation text
tools/switch-matrix.sh <binary>      # 529 invocations: the switch surface,
                                     # as each run's stderr plus the
                                     # settings it saves (see
                                     # REFACTORING.md, "The registry as
                                     # built")
tools/influence-matrix.sh <binary>   # the same for -j/-j0/-7 output
tools/graphics-matrix.sh <binary>    # 224 renders, nonzero if any
                                     # produced no file (work log item
                                     # 163); every graphics chart
                                     # mode, every option on three chart
                                     # types that draw differently, and
                                     # every output writer, one checksum
                                     # each. ~10 seconds
```

**They cover disjoint surfaces and none substitutes for another.**
The switch matrix never renders a chart, so charts0-3.cpp, intrpret.cpp
and the x*.cpp text paths are invisible to it; the chart matrix renders
only *text*, so the drawing code is invisible to both and wants the
graphics matrix (work log item 148) -- work log item 143
changed 1,055 formatting calls, mostly in exactly that code, and the
switch matrix was byte-identical over 75,471 lines while proving nothing
about them. Sabotage one site and re-run before trusting a clean diff:
a harness whose invocations all error also diffs to zero.

And three tables generated from the resource. Regenerate after any `.rc`
change; piping into `diff` is how you check they are still in sync, which
is the pre-commit form — the plain `>` form overwrites the committed file:

```sh
python3 tools/rc2qt.py astrolog.rc | diff - qtrcdlg.h                # dialogs
python3 tools/rc_accel.py astrolog.rc | diff - qtrcaccel.h           # accelerators
python3 tools/rc_cmd.py astrolog.rc resource.h | diff - qtrccmd.h    # cmd ids
```

```sh

# Ask the program a question directly -- the fast loop, ~0.2s. Rewrite
# ProbeQt() in qttest.cpp, rebuild, run. See QT_TESTING.md.
env -u DISPLAY QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= \
  ASTROLOG_QT_PROBE=1 ./astrolog-qt-test -i nrvate.as

# Driving a real window is 60-240s a run and only worth it for menus,
# focus, or the Windows build. Prefer the probe.
tools/qtdrive.sh run tools/scenarios/objectsel.txt
```

## The Windows build is the reference, and it runs here

Don't guess at Windows behaviour — build it and look. `Makefile.win`
cross-compiles the real Windows binary with mingw-w64, same
`wdriver.cpp`, same `astrolog.rc`, and it runs under Wine. This has
repeatedly settled questions that code reading got wrong.

```sh
make -f Makefile.win
tools/text-chart-capture.sh out/win         # Windows text charts
QTTEXTDIR=out/qt ./run-qt-tests.sh          # the same charts from this port
python3 tools/text-chart-diff.py out/win out/qt out/cmp
```

Graphics charts render headlessly too, to PNG, with no display and no
window manager:

```sh
QTGRAPHDIR=out/qtg ./run-qt-tests.sh   # 24 chart types, about 3 seconds
```

The Windows build has assertions of its own now — not many, and slow, but
it is no longer the only half with none:

```sh
tools/win-tests.sh                     # every tools/scenarios/win-*.txt
```

Minutes rather than seconds, so not a pre-commit check; run it when a
change ships in both builds. It swaps `/swe` for the bundled `ephem/`
first, because ~887,000 files through Wine's path translation looks
exactly like the app hanging. `QT_COMPARING_WITH_WINDOWS.md` says why.

The other slow check is the sanitizer sweep, over the two surfaces the
assertion suite barely reaches:

```sh
tools/asan-sweep.sh                    # both, ~750 invocations
tools/asan-sweep.sh switches           # the 529-invocation switch matrix
tools/asan-sweep.sh graphics           # ~230 renders
```

Also minutes, also pre-release rather than pre-commit, and it earns
that on its record: the first run of each half found real out-of-bounds
bugs in code exercised dozens of times without a sanitizer behind it
(work log items 133-134, seven between them, one crashing the release
build). It builds its own console binary with `-fsanitize=address` and
`-DQTTEST`, so the checked tables' range guards are live too. Read the
header before changing it — four of its arrangements are load-bearing,
including that the binary needs a *short* path while the output file
wants a *long* one. It also runs `make clean` on both sides of its
overridden build, because the plain Makefile shares the repo's object
directory — so **it deletes `./astrolog` while it runs**, and anything
else started against that binary meanwhile fails confusingly.

**ASan is not the whole of memory safety here, and the gap cost two
hunts.** That build is `-O0`, where glibc's `_FORTIFY_SOURCE` is
inactive — so a *fortify*-detected overflow (`__sprintf_chk` and its
siblings, the `*** buffer overflow detected ***` message) cannot fire
under it at all. Work log item 142 was exactly that, and survived 17 gdb
runs and a full ASan sweep because every hunt pointed at a build that
structurally could not see it. When the message is "buffer overflow
detected" rather than an ASan report, build optimized *with* symbols and
loop it under gdb; QT_TESTING.md has the one-line override.

**`QT_COMPARING_WITH_WINDOWS.md` has the full workflow**, including how to
drive either build headlessly and the environment traps that cost real
time to rediscover (**both** builds need a window manager — Qt for its
menus to open at all, Wine for a new dialog to accept keys; xdotool's
`--window` path uses XSendEvent, which Wine ignores; Astrolog's
accelerators are case-sensitive).

**Xvfb isolates the display, not the session's sound server.** Don't run
metacity on a capture display unless Qt needs it: it plays the X bell
through the user's speakers, and Astrolog rings the bell on every
keystroke it doesn't handle. When Qt does need it, start it as
`PULSE_SERVER=/nonexistent metacity --sm-disable &`.

On a private Xvfb display, `import -window root` is fine.

## Hard rules

- **Never put a `Claude-Session:` line in a commit message here.** History
  was scrubbed of it once already at the user's request.
  `Co-Authored-By:` is fine. Create new commits, don't amend.
- **Never push to `upstream`** (CruiserOne). Its push URL should read
  `DISABLED` — but that lives in `.git/config`, which does **not** survive
  a clone, so on a fresh checkout the guard is simply absent. Check
  `git remote -v` and set it yourself before doing anything else:
  `git remote set-url --push upstream DISABLED`. `origin` is the user's
  fork, `git@github.com:nrvate/Astrolog.git`; push `qt` there after each
  commit.
- **Never screenshot `import -window root` on the user's real desktop, or
  crop from it** — it leaks unrelated windows. Target a specific window ID
  found via `xdotool search --pid`, never by name substring. (A private
  Xvfb display is exempt; see above.)
- **The source is LF**, since 2026-09-01 (work log item 159) — the C++,
  the headers, the makefiles this fork owns, the tools and the docs. It
  used to be a 55/53 split with a per-file rule about preserving it, and
  that rule drew blood repeatedly (items 145, 158). Nothing in the source
  needed CRLF: converting left all 64 object files byte-identical, 31
  from g++ 11 and 33 from mingw g++ 10.

  **Four things are deliberately not LF, and `.gitattributes` lists each
  with its reason**: binaries (`.se1`, `.ttf`, `.pdf`, `.docx`, images);
  files Windows or VMS tooling owns (`.sln`, `.vcproj`, `.vcxproj`,
  `.rc`, `.def`, `.url`, `makefile.com`); the third-party `font/`
  distribution; and **the data files the program parses** (`.as`,
  `.csv`) — those last are not cosmetic, since converting them moved
  `tools/switch-matrix.sh` by six lines. Something in the data parsers
  reads a CR as content; until that is found they stay as they ship.

  `* -text` in `.gitattributes` stops a clone on Windows with the default
  `core.autocrlf=true` from rewriting anything, and
  `tools/line_endings_audit.py` fails on a CR appearing in the source.
  **Do not run a CR-stripping sweep over the tree**: one with a
  three-extension exclusion list corrupted 28 binaries — every `.se1`
  ephemeris and every `.ttf` — in exactly that way.
- **Prefer exact-string replacement over a range-based regex.** One
  range-based edit in this project matched across the gap between two
  functions and spliced them together, producing code that looked
  plausible and would not compile. If a match count is not exactly 1,
  stop.
- **Always test with `-i nrvate.as`**, the maintainer's own settings.
  It sets `-Yi1 "/swe"`, and `SwissEnsurePath()` caches the ephemeris
  path on first use — so without it at startup every esoteric body
  reads `???` and the run tells you nothing.
- The user's config at `/data/med/astrolog.as` is theirs. Don't edit it
  unless asked.
- **The port follows the desktop's dark mode**, because Qt5 has no API
  for it and its gtk3 platform theme supplies no palette. Detection is
  `NDarkPreferenceQt()` in `qtdriver.cpp`; `ASTROLOG_QT_THEME=dark|light`
  forces it either way, which is also how to check a change under both.
  Work log item 50 has the reasoning, including two `QSettings` traps.

## Working method

The things that have actually caught bugs in this project:

- **Verify a diagnosis before acting on it**, especially before touching
  shared core. Several confident wrong diagnoses are recorded in
  `QT_GUI_PLAN.md`; each was cheap to check and expensive to skip.
- **A test that passes both with and without the fix is worthless.**
  After writing a regression test, reintroduce the bug and confirm the
  test fails. This caught two tests in this project that were confirming
  an invention rather than catching a defect.
- **Verify new interactive behaviour live**, not just by code review.
  Multiple genuine bugs here only manifest at runtime.
- **Check for the compiler's failure, not for a word.** A build check
  matching `" error "` does not match `Error 1`, so a failing build reads
  as clean and every test after it runs against **the stale binary the
  failure left behind**. That happened here for a stretch, while the
  compiler was naming the exact problem. Match `: error:` and
  `^make.*\*\*\*`, and if a check claims to have rebuilt something,
  look at the binary's timestamp.
- **A regression test can be the regression.** A new test called a print
  routine outside `Action()`, so it wrote to a `FILE *` nothing had
  opened; glibc freed a buffer it never allocated and the suite began
  aborting six runs in twelve. An hour went into bisecting shared core
  for a heap corruption that was the test. ASan named it in one run after
  gdb had only shown where it surfaced -- reach for ASan on heap
  corruption, an optimized `-g` build on a fortify abort.
- **A harness proves nothing until you sabotage it either.** The same
  rule as above, one level up. A differential whose invocations all fail
  still diffs to zero, and reads exactly like a proof: `chart-matrix.sh`
  shipped its first draft with 15 of 70 invocations erroring on wrong
  switch arity, and `switch-matrix.sh` had capped its output at the first
  30 lines of a 159-line selection **for the whole T3 and phase-2
  campaigns** — every "byte-identical" in those work log items covered a
  fifth of what it claimed. Break one site on purpose and watch the diff
  move before believing a clean one.
- **A differential can only tell you something changed.** Every harness
  here compares the program to an older build of itself, or to the other
  build of the same core. None of that distinguishes "correct" from
  "unchanged", and it actively protects a wrong answer, because fixing a
  30-year-old bug shows up as a regression. When the question is whether
  a number is *right*, the reference has to come from outside the repo
  (REFACTORING.md T9, work log item 141).
- **Never `git checkout` a file to undo a sabotage** during a sweep or a
  falsification. It reverts that file's entire share of the change, not
  the one line you broke — twice in one session (general.cpp, then 204
  conversions in charts1.cpp, caught only because the totals stopped
  reconciling). Reverse-patch the exact string instead.
- **Prefer generating from `astrolog.rc` over transcribing by hand.** The
  dialogs, the 42 context menus and the menu mnemonics were all derived
  from it. Hand transcription introduced errors every time it was used.

`QT_GUI_PLAN.md`'s "Working pattern / verification methodology" section
has the long form, including the GUI-automation traps specific to this
setup — they are non-obvious and cost real time to rediscover.
