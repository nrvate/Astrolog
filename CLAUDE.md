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
- **`QT_CI_PLAN.md`** — CI, packaging and releases: what each job checks,
  how each was falsified, and the findings from building it. **Read it
  before adding or changing a job.** Not a plan any more — it is built,
  and the document is now the record of why each piece is shaped the way
  it is.
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
                                                    # (Qt Network too, same package;
                                                    # the makefiles stop and name
                                                    # the package if it is missing)
sudo apt install g++-mingw-w64-x86-64 wine           # build and run the Windows one
sudo apt install xvfb metacity xdotool imagemagick   # drive either headlessly
sudo apt install python3-pil                         # compare captures
sudo apt install nsis                                # build the Windows
                                                     # installer; makensis
                                                     # runs natively on
                                                     # Linux, and only
                                                     # *verifying* the
                                                     # result needs wine
```

Only the first line is needed to build the port and run its whole test
suite. The rest is for comparing against Windows.

**macOS is built only in CI**, on GitHub's `macos-14` runners, because
nobody working on this has a Mac. `tools/package-macos.sh` makes the
`.app` bundle and the `.dmg`; it needs `macdeployqt` from a Homebrew Qt
and nothing else. Notarization would need a paid Apple Developer account
and is **not** done -- the bundle is ad-hoc signed (`codesign -s -`),
which is mandatory on Apple Silicon and is enough to run, but a user's
first launch still goes through Gatekeeper's right-click-Open.

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
`-Yi3` at it.

**Since 2026-09-03 the bundled `ephem/` covers all 39 esoteric bodies on
its own**, so `-Yi1 ephem` and `/swe` resolve the same 39 and the suite
measures 3832/0 either way. It used to resolve 19 of 39, which meant CI
ran that group at half strength — 21 assertions where a local run did 41.
Closing it cost 7.8 MB and 20 files (`QT_CI_PLAN.md` Q13, option B).

`/swe` is still what `-i nrvate.as` reaches and still the larger set by
several orders of magnitude; what changed is that a run without it is no
longer quietly weaker. What a run without *any* ephemeris does is
unchanged and still the trap: the main planets keep computing, because
Moshier covers them — the Sun measured 0.04 arcsec off, 24Gem07'46.399"
against 46.440" — while **every esoteric body reads `0Ari00'00"`**. That
is measured, not assumed, and it is why the packaging checks assert on
Chiron and never on the Sun.

## Build and test

```sh
make qt -j4                      # ./astrolog-qt
make qt-test -j4                 # ./astrolog-qt-test
make wcli -j4                    # ./astrolog-wcli.exe, the console
                                 # Windows build: same core, entered at
                                 # main() rather than WinMain, so it runs
                                 # under Wine with no display at all
make all -j4                     # all five; "make clean" undoes it
                                 # (plain "make" is upstream's target and
                                 # still builds only ./astrolog, which ten
                                 # scripts here depend on)
make qt6 -j4                     # ./astrolog-qt6, against a hand-installed
make qt6-test -j4                # Qt6; QT6_PKGCONFIG says where it is.
                                 # Not in "make all" -- most machines have
                                 # no Qt6, and there the guard below would
                                 # stop the build asking for one.
                                 # These exist only because this machine's
                                 # Qt6 is off pkg-config's path: "make qt"
                                 # already picks Qt6 wherever pkg-config
                                 # can see it
QTTESTBIN=./astrolog-qt6-test ./run-qt-tests.sh  # the suite against Qt6:
                                 # the suite passes there too
make install                     # two wrappers on PATH, a menu entry and
                                 # icons; the data stays
                                 # in the checkout, so the tree has to stay
                                 # put (PREFIX=$HOME/.local needs no root)
./run-qt-tests.sh                # the whole suite + startup checks;
ASTROLOG_QT_TESTS=animation ./run-qt-tests.sh   # just one group, <1s
                                 # (=list names them; see QT_TESTING.md)
```

The six makefiles share one source list, `Makefile.srcs`: **add a
source file there, once**, in the group it belongs to, and no makefile
changes. Header dependencies come from the compiler (`-MMD -MP`), so
touching any header rebuilds exactly what includes it — that was not
true before 2026-09-01, when only `astrolog.h` and `extern.h` were
tracked and every other header rebuilt *nothing*. And `make clean` now
removes all five builds, which is a hazard with two sessions in one
tree; `make clean-console` is upstream's narrower one, which is what
`tools/asan-sweep.sh` uses.

`run-qt-tests.sh` is headless — no X display needed. Run it before every
commit. It prints its own count and that count grows every week, so this
document does not restate it — three documents once asserted three
different wrong numbers, which is what closed Q13 in `QT_CI_PLAN.md`. The
state that matters is **0 failed**, and CI runs it on every push. The
full suite is also clean under AddressSanitizer (`make qt-asan`) — but note that
build is `-O0`, where `_FORTIFY_SOURCE` is inactive, so it structurally
cannot see a fortify-detected overflow. Work log item 142 was invisible
to it for that reason and had to be caught in an optimized `-g` build.

What it covers: 25 dialogs open/close with the right titles, 42 context
menus resolve, 264 shortcuts bound and unique, 26 chart types render
non-blank, all 35 text chart modes survive 120-character chart names
and locations, the five import file formats parse and their long-line
truncation points hold, all 341 menu items fire without crashing, 258/258 Windows menu
items present, 256 show Windows' own accelerator text, 39/39 esoteric
bodies resolve against the ephemeris, the application icon resolves at
all three sizes, and bad input (missing files, unknown switches) doesn't
terminate the process.

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

Ten standing audits, all currently clean and all run by CI — four of the
port against `astrolog.rc`, one of the compiled defaults against
`astrolog.as`, one of the switch registry against the help text and
settings writer, one of round-trip fixture coverage, one of line endings,
one of the MSVC project against the makefile's source list, and one of
the Qt build's own source groups and headers:

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
python3 tools/qt_srcs_audit.py       # two checks on the MSVC Qt build,
                                     # which is a nightly and so is the
                                     # slowest place to learn anything.
                                     # No unguarded POSIX-only header in
                                     # any source it compiles -- that is
                                     # how qttest.cpp's <unistd.h> got
                                     # to a Windows runner, invisible to
                                     # every Linux net because on Linux
                                     # it is correct. And: tools/qt-srcs.py
                                     # asks Makefile.srcs
                                     # for exactly the groups the Qt
                                     # makefiles compile. It reads that
                                     # file, so a renamed group stops it
                                     # loudly -- but a NEW group would
                                     # leave it quietly one short, and
                                     # that surfaces as a link error on a
                                     # Windows runner attributed to
                                     # nothing
python3 tools/vcxproj_audit.py       # Astrolog.vcxproj lists exactly the
                                     # sources Makefile.win compiles. It
                                     # was one short for years, so MSVC
                                     # gave a link error nothing explained
```

CI runs all ten, plus a set of assertions that are scripts rather than
workflow steps so they can be falsified in a second instead of by
pushing. They are worth knowing about because several are useful by hand:

```sh
tools/ci-assert-fresh.sh astrolog.exe        # a build product is newer
                                             # than every .cpp and .h
tools/ci-assert-fortify.sh astrolog 10       # the shipped build still
                                             # imports glibc's *_chk
tools/ci-assert-toolchain.sh                 # the compilers match the
                                             # ones warnings.txt describes
tools/ci-assert-distinct.sh out/qtg 24       # no two renders identical
tools/ci-assert-installed.sh ~/.local/bin/astrolog   # run it from "/",
tools/ci-assert-uninstalled.sh ~/.local              # then undo it
tools/ci-assert-version.sh v8.00-qt.1        # a tag matches astrolog.h
tools/ci-verify-package.sh out/package/astrolog-windows
tools/ci-verify-linux-package.sh pkg.deb ubuntu:22.04  # install it in a
tools/ci-verify-repo.sh public                         # clean container
tools/ci-verify-live-repo.sh                 # and that the DEPLOYED site
                                             # serves it: a Pages deploy
                                             # can succeed and publish the
                                             # previous commit, with every
                                             # check upstream of it green.
                                             # By hand after a release,
                                             # never a gate -- propagation
                                             # is not instant and a check
                                             # retried until it passes is
                                             # not a check
tools/ci-differential.sh origin/qt out/diff  # four matrices vs a commit
tools/ci-verify-release-dist.sh dist 9       # the release ships exactly
                                             # nine artifacts and
                                             # SHA256SUMS covers them all
tools/ci-verify-windows-installer.sh out/astrolog-setup.exe  # install and
                                             # uninstall it under Wine
tools/ci-assert-clang-clean.sh               # the macOS compiler, whose
                                             # warnings are not gcc's
tools/ci-assert-green.sh                     # wait for CI on this commit
                                             # before tagging a release
tools/ci-assert-nightly.sh                   # is the SLOW lane healthy at
                                             # all -- a different question,
                                             # and the one nothing asked
                                             # while the nightly sat red
                                             # for days. Also fails if the
                                             # newest run is over 30h old,
                                             # because a schedule that
                                             # stopped firing reports no
                                             # failures and reads exactly
                                             # like one that passes
```

And an eleventh that is not fast and not resource-shaped: **the
compiler itself**, which nothing here read until 2026-09-01.

```sh
tools/warning_audit.py               # all five builds clean with -Wall,
                                     # every warning diffed against
                                     # tools/warnings.txt; about 70
                                     # seconds, not the six minutes this
                                     # said before anyone timed it. Plus
                                     # the two Qt6 builds, where there is
                                     # a Qt6, against warnings-qt6.txt --
                                     # which holds only what Qt6 warns
                                     # about and Qt5 does not, and is
                                     # empty. Skipped, not failed, on a
                                     # machine with no Qt6
tools/warning_audit.py --file io.cpp # one file, seconds, no baseline --
                                     # the loop to use while fixing; it
                                     # covers Qt6 too where present
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

And since 2026-09-03 the port itself runs on Windows, not only under
Wine. The nightly uploads `astrolog-Qt6-windows-msvc` -- the program and
the `-DQTTEST` build -- and:

```sh
WINVM_USER=... WINVM_PASS=... \
  tools/win-vm-suite.sh <vm> <unpacked-artifact> 'E:\swe'
```

copies it into a VirtualBox VM over the Guest Additions channel, **no
network involved**, and runs all 3,812 assertions headless. It passes
there: `PASS: 3812 passed, 0 failed`, 2026-09-03, the same count as
Linux -- 25 dialogs, 42 context menus, 264 shortcuts and the numeric
oracle, in a build with no `WIN` in it. Its header
lists four traps, each of which cost a run: the offscreen plugin has to
be IN the artifact (`windeployqt` ships only `windows`), the `windows`
plugin blocks forever because a guestcontrol process has no desktop, the
ephemeris must be on a **local disk** rather than a shared folder (a
negative lookup over `vboxsf` in that 887,000-file directory measured
1.9 s against 90 ms for a hit), and Qt6 does not start on Windows 7 at
all. `QT_COMPARING_WITH_WINDOWS.md` has the long form.

The other slow check is the sanitizer sweep, over the two surfaces the
assertion suite barely reaches -- and, since 2026-09-03, over the suite
itself:

```sh
tools/asan-sweep.sh                    # both, ~750 invocations
tools/asan-sweep.sh switches           # the 529-invocation switch matrix
tools/asan-sweep.sh graphics           # ~230 renders
tools/ubsan-sweep.sh                   # the same two surfaces under
                                       # -fsanitize=undefined, which
                                       # catches arithmetic UB ASan
                                       # cannot see. Currently clean
                                       # over 142 chart invocations and
                                       # 224 renders
```

**The one net here that can say a number is *right*** rather than
unchanged, from outside this repository entirely:

```sh
tools/swetest-oracle.sh ./astrolog /path/to/swetest "$PWD/ephem"
```

It builds nothing. Point it at a `swetest` compiled from **upstream**
Swiss Ephemeris -- `aloistr/swisseph`, `SE_VERSION "2.10.03"`, exactly
the version this tree vendors -- and it asks both programs the same 50
questions. Upstream at the same version means a disagreement is about
*Astrolog's integration*, not about two Swisses differing. It refuses to
run unless `sepl_18.se1`, `semo_18.se1` and `seas_18.se1` are actually
present, because upstream silently answers from Moshier when they are
not, and the difference hides in the last decimals (Sun on 15.6.1990:
84 deg 7'46.3995 from Swiss, 84 deg 7'46.4407 from Moshier). The nightly
clones and builds it in about 25 seconds.

**And a third surface the sweep does not reach**, added 2026-09-03 and
run by the nightly beside those two: the Qt suite itself.
`asan-sweep.sh` builds its own *console* binary, so all 3,812 assertions
-- 25 dialogs, 42 context menus, every menu item -- ran under no
sanitizer at all, even though `make qt-asan` had existed the whole time
and this file called the suite "clean under AddressSanitizer". It was
not: a mechanical POSIX sweep had left a `QByteArray` temporary's
`constData()` bound to a variable, and the read-after-free was invisible
on Linux and a SIGSEGV on macOS.

```sh
make qt-asan && ASAN_OPTIONS=detect_leaks=0 \
  env -u DISPLAY QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= \
  ASTROLOG_QT_EPHEM=minimal ./astrolog-qt-asan -Yi1 ephem
```

`detect_leaks=0` because a Qt harness that exits without unwinding its
widgets leaks by construction -- 10,769 bytes in 115 allocations, none of
it a defect this is looking for.

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

## CI, and what it will not let you do

Four workflows, all on `qt`, the default branch. `ci.yml` runs on every
push and pull request, fourteen jobs in about four minutes: the two
Windows builds, Qt5 and Qt6 builds with the suite, the audits and
generated tables, `make install`, a behavioural differential against the
base commit, the Windows package, two `.deb`s and four `.rpm`s.
`nightly.yml` is the slow lane -- six jobs: the warning audit, the
sanitizer sweeps (ASan and UBSan), the Windows parity harnesses, all four
matrices against yesterday, the external Swiss oracle, and two
`continue-on-error` experiments: **macOS**, which builds the port, runs
the suite and packages a `.dmg`, and **Qt on Windows**, which compiles
this port with MSVC and the open-source Qt6 (`-DQT -DPC`, no `-DWIN`) and
uploads the result so it can be run on a real desktop. Both have been
green for some time now; the flag stays because neither has a maintainer
who would notice a break.

`release.yml` publishes on a `v*` tag: nine artifacts -- four `.rpm`s,
two `.deb`s, a `.dmg`, the Windows `.zip` and the NSIS
`astrolog-setup.exe` -- each verified before publication, with a
SHA256SUMS that is checked to cover every one of them. `repo.yml` chains
off it on `workflow_run` and rebuilds the apt/yum repository from the
published packages.

The Windows experiment is worth knowing about for one reason: it is the
only net here that is a **different platform** rather than a stricter
tool on the same one, and that turned out to be a distinct kind of net.
Four sanitizer sweeps, ten audits and a warning ledger across five
builds never mentioned `qttest.cpp`'s `#include <unistd.h>`, because on
Linux it is correct. Compiling the file under MSVC found it in one run,
along with 15 `getpid()` and 11 hardcoded `"/tmp"` fallbacks behind it.
Two costs come with it, both measured rather than assumed: Qt6 does not
run on Windows 7 at all (`Qt6Core.dll` imports `SetThreadDescription`,
and the PE header claims minimum OS 6.0, so the obvious check lies), and
Qt 5.15.2 no longer compiles against a current MSVC.

**CI adds no logic.** Every step is a package install, a `make`, or one
committed script from `tools/`. A check living only in YAML can be
falsified only by pushing, which makes the falsification rule below
unaffordable -- so it does not happen. The scripts are
`tools/ci-*.sh`, and each runs in seconds on a laptop.

**Three things will fail a push, and each has bitten already:**

- **A behavioural change you did not declare.** The differential diffs
  chart, influence and graphics output against the base commit. If it
  moves, say so in a commit message:

  ```
  Behaviour-change: <one line on what moved and why>
  ```

  A differential answers "something changed", not "something broke", and
  it actively protects a wrong answer -- fixing a 30-year-old bug looks
  like a regression. So the opt-out is one line, not a workflow edit.
  The *nightly* differential reports rather than gates, because failing a
  day's aggregate punishes whoever pushed last.

- **A version that disagrees with its tag.** `astrolog.h` owns
  `szVersionFork`; the fork's version is `8.00-qt.N`. Bump it before
  tagging, or `tools/ci-assert-version.sh` stops the release.

- **A package that does not work.** Every `.deb` and `.rpm` is installed
  into a clean container and run from `/`, asserting **Chiron** -- never
  the Sun, which reads correctly with no ephemeris at all because
  Astrolog falls back to Moshier in silence.

**Two things to know before touching packaging.** The payload goes to
`/usr/lib/astrolog` with wrappers in `/usr/bin`, because Astrolog
resolves its data from the directory of its own executable. And the
distribution goes in the version -- `.rpm` gets it from `%{?dist}`, a
`.deb` needs the codename appended (`8.00+qt.1~jammy`), without which
both Ubuntu builds have the same filename and one silently overwrites
the other.

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
  Xvfb display is exempt; see above.) **Stop a test instance by the PID
  you started it with** -- `xdotool getwindowpid` if you only have the
  window -- never `pkill -f astrolog`: that matches the maintainer's own
  running copy, and did kill it once (work log item 169).
- **The source is LF**, since 2026-09-01 (work log item 159) — the C++,
  the headers, the makefiles this fork owns, the tools and the docs. It
  used to be a 55/53 split with a per-file rule about preserving it, and
  that rule drew blood repeatedly (items 145, 158). Nothing in the source
  needed CRLF: converting left all 64 object files byte-identical, 31
  from g++ 11 and 33 from mingw g++ 10.

  **Three things are deliberately not LF, and `.gitattributes` lists
  each with its reason**: binaries (`.se1`, `.ttf`, `.pdf`, `.docx`,
  images); files Windows or VMS tooling owns (`.sln`, `.vcproj`,
  `.vcxproj`, `.rc`, `.def`, `.url`, `makefile.com`); and the
  third-party `font/` distribution.

  There used to be a fourth — **the data files the program parses**
  (`.as`, `.csv`) — held back because converting them was measured to
  move `tools/switch-matrix.sh` by six lines, "something in the data
  parsers reads a CR as content". **That did not reproduce**, on six
  surfaces (work log item 173), and they are LF like everything else
  since 2026-09-02. The likely explanation for the original measurement
  is that the sweep which produced it also corrupted 28 binaries,
  including every `.se1` the charts are computed from.

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

  Since 2026-09-03 the user can also choose, in **View / Window Settings
  / Interface Theme**: System, Light or Dark. It is stored with
  `QSettings` in `IniFormat` rather than in the `.as` settings file,
  because it is window chrome rather than an astrological setting — the
  same reason Windows keeps its GUI preferences out of there. The
  environment variable deliberately outranks the saved choice, so a
  developer forcing one run does not disturb what the user picked.
  **This is the Qt build only.** `Makefile.win` builds `wdriver.cpp` with
  native Win32 menus, which follow Windows' own theming and know nothing
  about any of this.

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
- **`git add <file>` stages the file, not your change.** Two sessions
  share this tree. Staging a file wholesale committed another session's
  in-progress `&& fFalse` sabotage into `qttest.cpp`, silently disabling
  work log item 165's stale-`FILE` fix; nothing failed, because that abort
  was never deterministic. `git diff` what you are about to stage, and
  `git log -S` the suspect string when a line you did not write turns up
  in your own commit.
- **Prefer generating from `astrolog.rc` over transcribing by hand.** The
  dialogs, the 42 context menus and the menu mnemonics were all derived
  from it. Hand transcription introduced errors every time it was used.

`QT_GUI_PLAN.md`'s "Working pattern / verification methodology" section
has the long form, including the GUI-automation traps specific to this
setup — they are non-obvious and cost real time to rediscover.
