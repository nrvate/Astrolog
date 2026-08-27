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
- **`QT_COMPARING_WITH_WINDOWS.md`** — how to build and drive the real
  Windows binary under Wine and diff it against this port, and the
  headless-automation traps specific to doing that.
- **`QT_MENU_MAPPING.md`** — the Windows menu structure extracted from
  `astrolog.rc`, with command IDs.

The port lives in `qtdriver.cpp` (window, canvas, menus) and
`qtdialog.cpp` (dialogs), selected with `-DQT`, standing in for the
Windows-only `wdriver.cpp`/`wdialog.cpp`. The shared calculation and
drawing core is upstream's and is changed only when a backend branch is
genuinely missing (see "Editing shared code" below).

## Prerequisites

```sh
sudo apt install qtbase5-dev pkg-config              # build the Qt port
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
./run-qt-tests.sh                # 2773 assertions + startup checks
```

`run-qt-tests.sh` is headless — no X display needed. Run it before every
commit. Current state: **2773 passed, 0 failed**, startup diagnostics ok.

What it covers: 25 dialogs open/close with the right titles, 42 context
menus resolve, 263 shortcuts bound and unique, 26 chart types render
non-blank, all 337 menu items fire without crashing, 258/258 Windows menu
items present, and bad input (missing files, unknown switches) doesn't
terminate the process. A separate **Startup diagnostics** section runs
the binary as its own process, because an in-process suite cannot test
the startup that happens before its own event loop (see plan item 27).

Three audits, all currently clean:

```sh
python3 tools/rc2qt.py astrolog.rc > qtrcdlg.h   # regenerate dialog tables
python3 tools/rc_audit.py                        # controls nothing wires up
python3 tools/rc_mnemonic_audit.py               # "&" placement vs astrolog.rc
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

**`QT_COMPARING_WITH_WINDOWS.md` has the full workflow**, including how to
drive either build headlessly and the environment traps that cost real
time to rediscover (Qt needs a window manager for menus to open; xdotool's
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
  rewritten. Read with `newline=''` and write back the same way. Check:
  `tr -cd '\r' < file | wc -c` against `git show HEAD:file | tr -cd '\r' | wc -c`.
- The user's config at `/data/med/astrolog.as` is theirs. Don't edit it
  unless asked.

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
