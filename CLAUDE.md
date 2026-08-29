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

The port lives in `qtdriver.cpp` (window, canvas, menus) and
`qtdialog.cpp` (dialogs), selected with `-DQT`, standing in for the
Windows-only `wdriver.cpp`/`wdialog.cpp`.

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

## Build and test

```sh
make -f Makefile.qt -j4          # ./astrolog-qt
make -f Makefile.qt.test -j4     # ./astrolog-qt-test
./run-qt-tests.sh                # 3027 assertions + startup checks
ASTROLOG_QT_TESTS=animation ./run-qt-tests.sh   # just one group, <1s
                                 # (=list names them; see QT_TESTING.md)
```

`run-qt-tests.sh` is headless — no X display needed. Run it before every
commit. Current state: **3027 passed, 0 failed**, startup diagnostics ok. The full suite is also clean under AddressSanitizer (`make -f Makefile.qt.asan`).

What it covers: 25 dialogs open/close with the right titles, 42 context
menus resolve, 264 shortcuts bound and unique, 26 chart types render
non-blank, all 338 menu items fire without crashing, 258/258 Windows menu
items present, 256 show Windows' own accelerator text, 39/39 esoteric
bodies resolve against the ephemeris, and bad input (missing files,
unknown switches) doesn't terminate the process.

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

Four audits of the port against `astrolog.rc`, all currently clean:

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
```

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
- **Preserve CRLF in upstream files.** Most original Astrolog sources are
  CRLF; this fork's own files are LF. A scripted edit in text mode
  silently rewrites the whole file as LF and makes it diff as entirely
  rewritten. Read with `newline=''` and write back the same way.

  Check with `tr -cd '\r' < file | wc -c` **against that file's own line
  count**, not against `git show HEAD:file` — legitimately adding lines
  changes the CR total, so comparing to HEAD flags every honest edit and
  teaches you to ignore it. CR count == line count is the real invariant.
  (`sweph.cpp` is the one exception: it ships from upstream with 9 CRs in
  8621 lines. Not ours to fix.)
- **Never edit a CRLF file with a range-based regex.** One in this project
  matched across the gap between two functions and spliced them together,
  producing code that looked plausible and would not compile. Exact-string
  replacement only; if a match count is not exactly 1, stop.
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
- **Prefer generating from `astrolog.rc` over transcribing by hand.** The
  dialogs, the 42 context menus and the menu mnemonics were all derived
  from it. Hand transcription introduced errors every time it was used.

`QT_GUI_PLAN.md`'s "Working pattern / verification methodology" section
has the long form, including the GUI-automation traps specific to this
setup — they are non-obvious and cost real time to rediscover.
