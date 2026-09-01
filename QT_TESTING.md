# Seeing what this app actually does

How to answer a question about this program in **under a second**, and the
failure modes that waste hours if you reach for the wrong tool. Written
after a session that reached for the wrong tool repeatedly.

`QT_COMPARING_WITH_WINDOWS.md` covers diffing this build against the real
Windows one. This file is about observing *this* build.

## Always run with the real config

```sh
./astrolog-qt -i nrvate.as          # and the same -i for every tool below
```

`nrvate.as` is the maintainer's own settings file and is the only
realistic test input. It carries `-Yi1 "/swe"`, the path to the ephemeris
collection — **without it every esoteric body reads `???`**, and a `-Yi`
set after the first computation does not recover them. Not because of
Astrolog's own latch — `-Yi` clears `is.fSwissPathSet` and the path is
re-applied — but because the Swiss Ephemeris library caches its
orbital-elements state internally on the first (failed) load. Verified
live with a probe, 2026-08-29 (work log item 90): after a late
`-Yi1 "/swe"`, `rgszPath[1]` and the latch both update and Cupido still
reads 0. The path must be right before the *first* computation.

For old-vs-new *differential* work (proving a refactor changed
nothing), the method and its accumulated traps live in REFACTORING.md
under "The verification method" — including the `=` force-on prefix
requirement, grep's binary detection on chart output, the
parallel-make grep race, and the fact that `-od` never persists
AstroExpression hooks, so a settings-file leg cannot verify `-~*`
stores. It also carries the
restrictions, orbs, aspect set and 13 macro names that make the program
behave the way it does in use. Testing against the stock `astrolog.as`
answers a question nobody has.

## The fast way: a scratch probe inside qttest

**This is the preferred method and it is not close.** A round trip is
about **0.2 seconds**.

`ProbeQt()` in qttest.cpp is a deliberately empty function. Rewrite its
body to ask the program a question, build, run, read the answer, rewrite
it again:

```c
static void ProbeQt()
{
  char sz[cchSzMax];
  int n, t;

  SzObjSelName(sz, 1, 52872);            // anything in the program is here
  printf("52872 -> %s\n", sz);
  printf("back:  %s\n", FObjSelRecall("Okyrhoe", &t, &n) ? "yes" : "no");
}
```

```sh
make -f Makefile.qt.test -j4
env -u DISPLAY QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= \
  ASTROLOG_QT_PROBE=1 ./astrolog-qt-test -i nrvate.as
```

Why it beats driving the UI: **there is no UI**. No display, no window
manager, no input simulation, no focus, no coordinates, no sleeps, nothing
to miss. The probe runs inside the real program after the window, menus
and first chart are up, so every global is live — poke `us`/`gs`/`gi`
directly, call `SetChartModeQt()` or any dialog function, run switches
through `FProcessCommandLine()`.

The body is **scratch**. Rewrite it freely; nothing depends on it. Do not
put assertions in it — those belong in the suite.

For anything visual, save `gi.qim` and then **measure the PNG rather than
looking at it**:

```python
from PIL import Image
from collections import Counter
im = Image.open("probe.png").convert("RGB"); px = im.load(); w, h = im.size
print(Counter(im.getdata()).most_common(4))
for x in range(w-1, 0, -1):                   # find the sidebar edge
    if any(px[x, y] != (0, 0, 0) for y in range(0, h, 5)):
        print("chart width", x+1); break
```

A worked example of the whole loop: "if the GUI shows me Okyrhoe, will it
accept Okyrhoe back?" — probe written, built, run, answered in 0.21s,
across six esoteric bodies at once.

## Running one test group instead of all of them

The suite runs 45 groups in sequence and takes tens of seconds end to
end. When one group is under investigation, run just it:

```sh
ASTROLOG_QT_TESTS=animation ./run-qt-tests.sh       # one group, well under a second
ASTROLOG_QT_TESTS=objsel,glyph ./run-qt-tests.sh    # comma = alternatives
ASTROLOG_QT_TESTS=list ./run-qt-tests.sh            # print the group names
```

Matching is a case-insensitive substring over the names in `rgqttestQt[]`
(qttest.cpp). A filter that matches nothing fails the run rather than
reporting an empty PASS, so a typo cannot read as success.

When a filter is active — or `ASTROLOG_QT_TIME=1` on a full run — each
group prints its wall time. Measured once: `menu-actions` is ~60% of the
whole suite by itself (it fires all 338 menu items), with `objsel-dialog`
and `chart-render` most of the rest; nearly every other group is
single-digit milliseconds. So "the suite is slow" is really "four groups
are slow", and a debugging loop that avoids them is interactive.

Four caveats, all already paid for:

- **A group that passes alone and fails in the full run is inheriting
  state.** `menu-actions` leaves every setting wherever firing 338 items
  lands, and anything after it must set what it depends on. Dump the
  globals in a solo run and a full run and diff them (work log item 57)
  rather than guessing one variable per rebuild.
- **Never run two suites at the same time.** The regular and ASan
  binaries (and any capture run) write the same fixture paths under
  `$TMPDIR` — `astrolog-qt-longstrings.txt`, `astrolog-qt-roundtrip.as`
  and friends — so concurrent runs corrupt each other and fail groups
  that pass alone. It looks exactly like a real regression, and it cost
  an hour of bisecting before the collision was noticed (work log item
  117). One suite at a time, and `pgrep -fa astrolog` for orphans first.
- **The suite's result must not depend on files outside the repo.** The
  menu sweep fires the user's macros, and a macro is a `-i` load of
  whatever file the user's config names — whether those files *exist*
  changed the suite's result once (work log item 117; three groups went
  red with no code change). The sweep now restores the custom-slot state
  macros can touch; anything else a fired action reads from outside the
  tree should either be snapshotted the same way or not asserted on.
- **An intermittent crash localises in seconds this way.** The exit-time
  heap corruption of 2026-08-28 took eight ~40-second full runs to pin to
  one test; three sub-second runs of that group alone would have answered
  it. Prefer this to any loop over the full suite — and prefer capturing
  one run's output to a file over re-running because a grep pattern
  missed.

## Rendering charts headlessly

The same idea, already packaged, for when you want the images themselves:

```sh
QTTEXTDIR=out/qt   ./run-qt-tests.sh    # 8 text charts    -> PNG, ~2s
QTGRAPHDIR=out/qtg ./run-qt-tests.sh    # 24 graphics      -> PNG, ~3s
```

Both render and exit without running the assertion suite. `gAspect` and
`gArabic` are absent from the graphics set on purpose: `DrawChartX()` has
no case for either, which is why Windows forces text mode for exactly
those two.

**Byte-diffing captures requires pinning the chart time.** The captures
cast whatever chart the startup arguments produce, and with just
`-i nrvate.as` that is *now* -- two captures from the very same binary
differ in 23 of 24 images (only `calendar.png` survives). A diff that is
supposed to prove a change harmless must pin the moment, and extra
arguments pass straight through `run-qt-tests.sh`:

```sh
QTGRAPHDIR=out/a ./run-qt-tests.sh -i nrvate.as   -qb 3 15 2020 10:30 0 6:00 87W39 41N51
```

With the time pinned the images are reproducible to the byte, so
`diff -rq` between a worktree build of the old commit and the tree's
build is a real proof (work log items 109 and 111 both did exactly
this -- 111 re-derived the trap from scratch because 109 had recorded
it only in the work log, which is why it is written here now).

## When the suite itself takes the process down

An assertion failure prints and carries on. A *crash* does not, and the
log then ends wherever the buffer happened to stop, which is rarely the
thing that killed it. Two of the suite's sections name each item before
they touch it and flush, so the last line printed is the culprit:

```sh
ASTROLOG_QT_TEST_VERBOSE=1 ./run-qt-tests.sh
```

That covers the 26 chart renders (`rendering: TraNatGra`) and all 338
menu items fired (`firing: Chart Settings...`). A clean run does not need
the noise, which is why it is off by default — reach for it the moment a
run dies without saying where.

**One kind of death names itself.** The checked tables (`TBLSIG`,
`TBLOBJ`, `TBLSIGRAY`, and the aspect family's `TBLASP`, `TBLASPR`,
`TBLASPB`, `TBLASPK`) range check their subscript under the test build
and abort on a bad index, printing the struct and the astrolog.h line:

```
astrolog-qt-test: astrolog.h:847: int& TBLOBJ::operator[](OBJT):
  Assertion `(i.n) >= 0 && (i.n) <= (...)' failed.
```

That is a real bug in the code under test, never a broken test — an
index of the right domain running off the end of its table. A
*regression* test for such a bug therefore fails as SIGABRT rather than
as a FAIL line, because the assert fires before the test's own
comparison runs; `ray-digit-fill` is the worked example and says so. It fires
only under `QTTEST` (so the test and ASAN builds; work log item 127),
which is why the same input can be quiet in `./astrolog-qt`. Debug it
there rather than concluding the release build is fine.

## The console build, for the calculation core

`make` builds the X11 binary, the quickest way to exercise the shared
calculation and text rendering with no GUI in the way:

```sh
env -u DISPLAY ./astrolog -n _X \
  -qb 11 19 1971 11:01am 0 8W 122:19:59W 47:36:35N </dev/null
```

`_X` forces text, so a settings file carrying `=X` does not open a window
and sit there; `-qb` pins the chart so runs are comparable; `</dev/null`
makes a prompt fail fast rather than look like a hang; unsetting `DISPLAY`
makes a stray graphics switch error out visibly.

**It loads `nrvate.as` now**, which it could not until 2026-08-27:
`case 'W':` in astrolog.cpp was `#if defined(WIN) || defined(QT)`, so this
build rejected `-WM` as an unknown switch, and an unknown switch stops
Astrolog reading the rest of the file — it gave up 240 lines in and used
defaults for everything below. So the real config, restrictions, orbs and
the `/swe` path are all available here:

```sh
env -u DISPLAY ./astrolog -i nrvate.as _X -n </dev/null | head
```

Still no GUI in it, so it answers nothing about windows, dialogs or menus
— that is what the probe and the drivers are for. What it is good for is
the calculation core and text output with the least machinery in the way,
and being an ordinary process, it takes a debugger without an event loop
in the picture.

## Driving a real window — slow, clunky, occasionally necessary

`tools/qtdrive.sh` and `tools/windrive.sh` launch the app for real and
drive it. **A run costs 60 to 240 seconds against the probe's 0.2**, and
they carry every failure mode the probe does not have. Reach for them only
when the thing under test genuinely *is* the windowing behaviour:

- a menu actually opening, and its mnemonic
- initial keyboard focus
- what a dialog looks like, for a human to review
- the Windows build, which has no other automation at all

For the Windows build that last one is packaged as `tools/win-tests.sh`,
which runs every `tools/scenarios/win-*.txt` against the real config and
reports once. It is minutes, not the suite's seconds, so it is not a
pre-commit check — run it when something ships in both builds.

For anything else — what a field parses, what a global holds, what a chart
draws — use the probe. It answers the same question hundreds of times
faster and cannot mis-click.

```sh
tools/qtdrive.sh tree --args "-i nrvate.as"
tools/qtdrive.sh run tools/scenarios/objectsel.txt
tools/windrive.sh run tools/scenarios/win-objectsel.txt
```

`qtdrive.sh` sets up the display, window manager and private D-Bus, then
runs `tools/qtdrive.py`, which is where the scenario commands and the
AT-SPI walking actually live — edit the `.py`, not the wrapper.

It addresses widgets by name over AT-SPI, which is at least
better than measuring screenshots: Qt draws every widget inside one X
window, so `xwininfo` and `xdotool` see the window but not the buttons in
it, and a mis-measured click leaves the dialog open while a before/after
comparison reports "identical" because nothing happened. Scenario
commands: `menu`, `click`, `type`, `typeinto`, `expect-window`,
`expect-no-window`, `expect-value`, `shot`, `tree`, `sleep`.

Things that cost a cycle each, if you do end up here:

- **Qt only publishes its widget tree when `ScreenReaderEnabled` is true.**
  The app appears on the bus regardless, reporting zero children, which
  looks like no tree at all. `qtdrive.sh` sets it on a private bus.
- **A widget's accessible name is its current *value*, not its
  astrolog.rc symbol.** `deOs01` means nothing to AT-SPI. Buttons, menus
  and labels are reachable by name; identical rows need `role#n`, 1-based,
  hyphenated because scenario lines split on whitespace: `combo-box#5`.
- **A QComboBox is not editable through AT-SPI** — its internal line edit
  is — so `type` cannot reach one. `typeinto` clicks and uses the keyboard.
- **Wine needs a window manager too.** Not for rendering, for *input*: a
  dialog Wine has just created does not become the X focus window, so keys
  keep going to the main window and it ignores everything, Escape
  included.
- **The `expect-*` lines are the whole point.** A keystroke or click that
  lands nowhere is completely silent. Every assertion in both drivers was
  verified by making it fail on purpose.
- **`pkill -f` matches your own command line** and kills the shell running
  it, surfacing as a bare exit code 144. Use `pkill -x`, and remember
  `/proc/pid/comm` truncates at 15 characters: `astrolog-qt-tes`.
- **The file dialogs are native GTK and AT-SPI cannot see into them** —
  their tree is unnamed buttons at bogus coordinates. The `key` and
  `typeraw` scenario commands are the escape hatch: `key ctrl+l`, then
  `typeraw /path/to/file.as`, then `key Return` drives the path bar
  every GTK chooser has.
- **A QAction with a shortcut reports the shortcut inside its accessible
  name** (`Open Chart...\tAlt+o`), which is why the driver matches on
  the label half before the tab. If a by-name lookup misses a menu item
  that clearly exists, check the name for a `\t` first.

## One slow thing that is not a bug

A JPL Horizons lookup — the `j<n>` object definition, and Lookup Names on
such a row — takes about a second the first time. That is the service, not
this program: measured with `ASTROLOG_QT_NETLOG=1`, headers, first byte
and completion all land on the same timestamp roughly 900ms after the
request goes out, so the ~7KB reply transfers instantly and the entire
wait is Horizons computing the ephemeris before it answers.

The parts that *were* ours are done: the TLS handshake (130ms) is paid
once per session rather than once per body, and a body-and-time already
asked about is answered from cache in 0ms. Nothing else on this end
moves the number. Don't go looking for a bug in it.

Both shipped configs carry `=0n`, which disables web queries entirely, so
none of this happens unless someone asks for it.

## When something stops rather than slows

**Any `PrintWarning()`/`PrintError()` becomes a modal dialog and waits
forever for a click nobody is there to give.** Three disguises: an
automated run that stops partway and never returns; the Windows build
painting its menu bar and nothing else; a chart mode that "renders slowly"
when it is not rendering at all.

`SetNoPopupQt(fTrue)` is the escape, which the captures and
`TestBadInputQt()` use. On the Windows binary it is `-Wt`. There is no
command-line escape in the Qt build.

Recognising it from outside, with no debugger:

```sh
P=$(pgrep -x astrolog-qt-tes)
cat /proc/$P/wchan                     # do_poll...
ls -l /proc/$P/fd | grep -c socket     # 0
```

Sleeping in `do_poll`, no sockets, no CPU: that is a Qt event loop waiting
on a window. It is a dialog. **`timeout`'s exit code 124 means the process
was still running, not that it was stuck.**

**Crop the whole frame, not the corner.** A message box is centred; chart
text is top-left. A crop of the top-left corner shows a blank client area
and hides the dialog explaining it, which is how the above went unseen
through several wrong theories. Better still, measure — `non-black rows:
0..55` out of 1200 says "menu bar only" without anyone having to look.

### The same symptom, a second cause: /swe under Wine

The Windows build given `nrvate.as` paints its menu bar, leaves the client
area black and ignores every keystroke — identical to the modal dialog
above, and this time there is no dialog. The cause is `-Yi "/swe"`:
**887,000 ephemeris files**, which native Linux handles and Wine's path
translation does not. It is neither a hang nor an app bug, and `-Wt` does
nothing for it.

The tell that separates the two: screenshot the centre. A dialog is there
or it is not. Then swap the path and see it draw:

```sh
sed 's|"/swe"|"ephem"|' nrvate.as > /tmp/w.as
tools/windrive.sh run tools/scenarios/win-objectsel.txt --args "-i /tmp/w.as"
```

`tools/win-tests.sh` does that swap itself. Everything worth testing in
the config — restrictions, orbs, aspect set, macros, window size, graphics
mode — survives it; only the file count goes.

This one masqueraded as two other things first. Both were wrong and both
are worth not repeating: *"XTEST accelerators don't reach Wine"* (they do —
`ctrl+t`, `shift+alt+a` and `alt+j` all drive dialogs in
`win-dialogs.txt`), and *"graphics mode doesn't render under Wine"* (it
does; every earlier capture used `_X` for unrelated reasons, so it had
simply never been tried). Both theories came from changing two things at
once. Change one.

And time each step before theorising. Every graphics mode draws in 1–60ms
except Rising (~490ms) and the two transit grids (~275ms); each PNG saves
in 30–55ms. If something takes seconds, it is not drawing.

## Finding memory bugs: build the suite under AddressSanitizer

A fault that happens once and will not happen again is almost never a
logic error you can read your way to. ASan makes it deterministic — it
names the line, the array, and how far outside it the access went, on the
run where the damage is *done* rather than the later run where the
program falls over.

**But ASan is not the whole of it, and one blind spot cost two hunts.**
That build is `-O0`, and glibc's `_FORTIFY_SOURCE` is inactive without
optimization — so a *fortify*-detected overflow (`__sprintf_chk` and
friends) cannot fire there at all. Work log item 142 was exactly that: an
intermittent `*** buffer overflow detected ***` that survived 17 gdb runs
and a full ASan sweep of the switch matrix, because every hunt was
pointed at a build that structurally could not see it. What found it was
an optimized build with symbols — a one-line override, no makefile edit:

```sh
make -f Makefile.qt.test NAME=astrolog-qt-dbg OBJDIR=obj-qt-dbg \
  CPPFLAGS="-DQT -DQTTEST -O -g -fPIC -Wno-write-strings -Wno-narrowing \
  -Wno-comment $(pkg-config --cflags Qt5Widgets Qt5Network Qt5Test)" -j4
```

then loop it under `gdb -batch -ex 'run -i nrvate.as' -ex bt` until it
aborts. **So: ASan for reads and heap, an optimized `-g` build for
fortify aborts.** Reach for the second whenever the message is
"buffer overflow detected" rather than an ASan report.

**`Makefile.qt.asan` already exists for this.** It is `Makefile.qt.test`
with `-fsanitize=address -g -O0` and its own name and object directory,
so it never clobbers the normal build:

```sh
make -f Makefile.qt.asan -j4
env -u DISPLAY QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= \
  ASAN_OPTIONS=detect_leaks=0 timeout 600 ./astrolog-qt-asan -i nrvate.as
```

`detect_leaks=0` matters: Qt does not free everything at exit and the
leak report buries any real finding. Turn it on only when leaks are what
you are hunting. Expect minutes rather than seconds — hence the
`timeout`, which is not optional courtesy: a run that goes wrong here
goes wrong by *hanging*, not by failing.

**Point it at the switch matrix, not only the suite** --
`tools/asan-sweep.sh switches` does exactly this. A console build
with `-fsanitize=address -DQTTEST` driven through
`tools/switch-matrix.sh` covers 529 invocations of the whole switch
surface, including the error and edge shapes, and on its first ever run
it found two real out-of-bounds bugs the suite had never reached (work
log item 133) -- one of which segfaults the release build. Two traps:
the plain `Makefile` has no object directory, so `make clean` before
and after (see Build traps), and the matrix pipes each run's stderr
through `head -2`, so a report arrives decapitated -- it tells you the
invocation, and you re-run that one directly to get the trace.

**And at the graphics surface, which renders rather than parses.** The
switch matrix drives the -X family but sends stdout to /dev/null, so it
covers their parsing and hardly any drawing. A sweep that renders every
graphics mode, every option on three different chart types, and each
output writer to a file -- 254 invocations -- found five more
out-of-bounds bugs on its first run (work log item 134), one of which
aborts the release build.

**Both halves are `tools/asan-sweep.sh` now** (work log item 138) --
`switches`, `graphics`, or neither argument for both. Prose describing
a check is not a check: these two found seven bugs and then existed
only as a paragraph, which is the same failure the divergence list had.

The suite is a good host because it already renders 26 chart types and
fires all 338 menu items, so one run exercises far more than a person
clicking could. `ProbeQt()` works under ASan too, which is how the
second instance of the esoteric-influence bug was confirmed rather than
merely suspected from reading. Between them, ASan has found four real
out-of-bounds bugs in this program's own chart code (plan items 24 and
37), every one of them silent in a normal build.

**Open, unattributed (2026-08-30):** one run during work log item 128
reported a `global-buffer-overflow` and then would not repeat — six
further runs of the same binary were clean, so no stack trace was ever
captured. Two things are known about it. It is **not** on a checked
table: E2's range asserts are live under ASan and would have aborted
by name first. And the faulting `pc` sat in a shared library while the
address sat in the binary's own data, which is the signature of an
intercepted libc call (`memcpy`/`strlen`/`sprintf` and kin) running
off a global — so look at the `CopyRgb`/`ClearB` boundaries and the
`sprintf` sites, not at a subscript. The suite is a plausible host for
an intermittent: `TestAllMenuActionsQt()` leaves every setting where
338 menu items put it, and several charts are cast for *now*. Don't
burn a session looping the suite for it — it will come around, and
next time keep the whole log.

**It came around on 2026-08-30, and narrowed.** A plain
`./run-qt-tests.sh` aborted with glibc's `*** buffer overflow
detected ***`, then passed twice straight after. That is a second
detector agreeing with the first, and it sharpens the target:
`_FORTIFY_SOURCE` only instruments the `__*_chk` family — `sprintf`,
`strcpy`, `memcpy` and kin — so this is a **formatting or copy overrun,
not a stray subscript**, which matches the ASan report's
library-`pc`-into-binary-data shape. Both times the run before and
after was clean, so something in the suite's own accumulated state
reaches it rather than any one group. When it next fires, keep the
whole log and the `ASTROLOG_QT_TEST_VERBOSE=1` output.

**Hunted deliberately on 2026-08-31, and not found.** Everything below
came back clean, which narrows what it can be without closing it:

- The suite under `-O2 -D_FORTIFY_SOURCE=3` — strictly stronger than
  the `-O` build that reported it, since `=3` adds dynamic
  object-size checks `=2` cannot do. Clean.
- The 529-invocation switch matrix and a 140-render graphics sweep
  under that same build, plus the checked tables' range guards. Clean.
- The suite under ASan. Clean.
- 17 runs under gdb, and the object core's range guards (work log
  items 135-137), which now cover ~1,800 subscripts. Clean.

Two candidates were checked and one survives. `WriteXBitmap()`'s
80-byte path buffer (item 134) is the right *shape* for a fortify
abort, but instrumenting it proves the suite never calls it at all —
in a normal run or a `QTGRAPHDIR` capture. `XChartAstroGraph()`'s
`ret[i]` overread (item 136) fits the ASan sighting well: `cp0.dir` is
a global, and it is intermittent for the right reason. It cannot
explain the fortify one, which only fires on the `__*_chk` family.

**One mundane possibility deserves naming**, because that day involved
repeated builds with overridden flags into the shared object directory
(see Build traps): a link mixing objects compiled against different
struct layouts produces exactly a spurious fortify abort. If it never
recurs on a clean tree, that is the likeliest answer, and it is not a
bug in the program.

Two things that cost time here:

- **ASan stops at the first error**, so a fix can reveal the next one
  immediately behind it — line 1342 hid line 1456 entirely. Re-run after
  every fix until it is silent. One clean report proves nothing if the
  first error aborted the run.
- **Everything runs about ten times slower**, which loses races the
  normal build wins. The suite's modal-closing timers scale off
  `__SANITIZE_ADDRESS__` for exactly that reason; before they did, a
  dialog appeared after its own auto-close timer had fired and the run
  waited forever for a click nobody was there to give. If an ASan run
  seems endless, check it the way "When something stops rather than
  slows" says to before assuming it is just slow: 39 seconds of CPU
  against 19 minutes of wall clock is a dialog, not a sanitizer.

## Build traps

- **`Makefile.win`'s resource depends on `resource.h`.** Change control IDs
  without rebuilding the `.res` and the compiled dialog template and the
  C++ addressing it disagree — every field lands in the wrong column,
  which reads exactly like broken layout code. No audit here can see it.
- **Regenerate `qtrcdlg.h` after any `.rc` change** and rebuild.
- **Grep build output for `: error`**, not a narrower pattern.
- **The console build's objects live in the repo root** (plain
  `Makefile`), separate from `obj-qt*/`. Rebuilding the Qt binaries does
  not touch them, so after editing shared core, `./astrolog` is stale
  until its own `make` runs — a stale console binary once "reproduced" a
  crash the fix had already cured (work log item 115). `make` also
  links with `-s`: for a symbolized backtrace, rebuild with
  `LIBS="-lm -lX11 -ldl" CPPFLAGS="-O -g ..."`.
- **A one-off console build with overridden flags clobbers those same
  objects.** There is no separate object directory, so
  `make NAME=/tmp/x CPPFLAGS="-fsanitize=address ..."` leaves sanitized
  `.o` files in the repo root and the next ordinary `make` fails to
  *link*, naming functions in `general.o` rather than anything you
  touched (work log item 129). `make clean` before such a build and
  after it. The same recipe is how to get a **console** ASan binary,
  which the Qt-only `Makefile.qt.asan` does not give you — add
  `-DQTTEST` to that `CPPFLAGS` and the checked tables' range asserts
  come with it, which is what caught item 130. Build it at a **short
  path**: a deep one truncates the ephemeris path and changes lookups.
- **Never edit a CRLF file with a range-based regex.** One in this project
  spliced two functions together by matching across the block between
  them. Exact-string replacement only, then check
  `tr -cd '\r' < f | wc -c` equals the line count. Note `sweph.cpp` ships
  from upstream with mixed endings (9 CRs in 8621 lines); not ours to fix.

## Proving a regression test actually works

A test that passes with and without the fix is worthless, and this
project has produced two of them — each confirming an invention rather
than catching a defect. After writing one, put the bug back and watch it
fail. It costs one rebuild.

Back the files up, patch, build, run, restore:

```sh
mkdir -p /tmp/bk && cp calc.cpp extern.h /tmp/bk/
#   ... reintroduce the bug, rebuild, run, expect FAIL ...
cp /tmp/bk/calc.cpp /tmp/bk/extern.h .
```

Two ways this goes wrong, both seen here:

- **Restoring the bug is not the same as disabling the fix.** Proving the
  forced-midpoint fix meant putting back a `&&` term that had been
  negated. Writing `fFalse` there disables the skip altogether — a third
  behaviour that is neither the bug nor the fix — and the test passed
  either way. `fTrue` was the restore. Work out what the original
  expression evaluated to, not what makes the line look inert.
- **Most of these files are CRLF.** An exact-string patch written with
  bare `\n` matches nothing, and a script that asserts on the match count
  says so immediately; one that does not will happily report a pass
  against an unmodified file. Assert `count == 1` before every write.

Then state the test's preconditions rather than inheriting them. The
suite shares live `us`/`gs` state with everything that ran before it, so
a setting an earlier test left on can make a correct fix look broken —
`-4` left set put a forced midpoint exactly 30 degrees off, which reads
precisely like the fix not working.

**Restore by reverse-patching the exact string, never `git checkout`.**
That reverts the file's *whole* share of whatever you are working on, not
the one line you broke. It happened twice in one session: once silently
undoing 204 conversions in charts1.cpp, caught only because a running
total stopped reconciling. And read the saved original back in binary or
with `newline=''` — Python's default text mode strips CRs and quietly
turns your restored line into the only LF line in a CRLF file.

## Where a regression test belongs, which is not always in the suite

Three defects fixed on 2026-08-31 needed three different kinds of net,
and choosing wrong is what made the day long. Work through it in this
order:

1. **Does the function write into a destination you can inspect?** Then
   it is an ordinary in-suite `Check()`. `FormatSz()` takes its buffer as
   a parameter, so the test hands it a small one and asserts the result
   stayed inside.
2. **Does it need a chart drawn?** Still in-suite — `SetChartModeQt()`
   renders to `gi.qim` with no display. That is how the 3,000-character
   sidebar is checked.
3. **Does it end in `PrintSz()`?** Then it **cannot** be called from the
   suite at all. `is.S` is opened and closed by `Action()` and by nothing
   else, so an in-suite call writes to a stream that is not open and the
   process dies of heap corruption later, somewhere unrelated. Either go
   through `Action()` with `is.szFileScreen` set (as
   `TestLongStringsQt()` does) or, if the defect is reachable from a
   switch, make it an invocation in `tools/switch-matrix.sh` — where a
   crash at the old commit *is* the behavioural diff that proves the fix.

**A test that crashes the suite is worse than no test**, because the
crash looks like a regression in the code under test. When the suite
starts aborting right after you add one, suspect the test first: point
ASan at it, which names the caller in one run, where gdb only shows
where the damage surfaced.

**And do not put a check where it cannot fail.** `-YXt` was added to the
switch matrix next to `-YYt` and produced no diff at all, because that
harness never renders a chart and the switch only stores a string.
Measured, then removed. Before believing a new harness entry, break the
thing it watches and confirm the entry moves.

## Checks worth running before a commit

```sh
make -f Makefile.qt -j4 && make -f Makefile.qt.test -j4 && ./run-qt-tests.sh
make -f Makefile.win -j4
python3 tools/rc_audit.py
python3 tools/rc_mnemonic_audit.py
python3 tools/rc_field_audit.py
python3 tools/rc_lookup_audit.py
python3 tools/defaults_audit.py
python3 tools/registry_audit.py
python3 tools/rc2qt.py astrolog.rc | diff - qtrcdlg.h
python3 tools/rc_accel.py astrolog.rc | diff - qtrcaccel.h
python3 tools/rc_cmd.py astrolog.rc resource.h | diff - qtrccmd.h
tools/settings-round-trip.sh
```

The three `diff` lines check that the generated tables still match the
resource. Run the generators with `>` instead only when you mean to
regenerate them, and rebuild afterwards: the object files depend on those
headers, and a regeneration without a rebuild looks like it did nothing.

And when the change touches both builds, `tools/win-tests.sh` as well.
The shared logic underneath is already covered by the Qt suite, since
both builds call the same `calc.cpp` and `io.cpp`; what this adds is the
Windows dialog and menu wiring, which nothing else sees.

## The compiler is a harness too, and was unread until 2026-09-01

```sh
tools/warning_audit.py                 # all four builds, ~6 minutes
tools/warning_audit.py --file io.cpp   # one file, seconds, while fixing
tools/warning_audit.py --update        # after a fix, to move the ledger
```

It compiles console, Qt, Qt-test and Windows clean with `-Wall`,
normalizes each warning to (build, file, function, flag, message with the
numbers masked) and diffs that against `tools/warnings.txt` — 462
warnings in 131 sites as of 2026-09-01, down from 857. It fails on a **removed** line as
well as an added one, so fixing something means regenerating the ledger
and the ledger cannot drift into overstating what is left.

Three things about it are worth knowing before you use it.

**It is not pre-commit.** Four clean builds is minutes, like
`tools/asan-sweep.sh` and `tools/win-tests.sh`. Use `--file` while
working — it compiles one source file under all four flag sets in
seconds — and the full run before a commit that touched a lot of code.

**Subset runs are not a gate.** `--build console` prints its report and
refuses to compare, because the first column is the *set* of builds that
agree on a site: auditing one build renames every shared row and the diff
would be pure noise.

**The Windows half cannot see the format classes.** mingw redirects
`snprintf` to `__mingw_snprintf`, which GCC does not treat as the
builtin, so format analysis is silently absent — 45 format-truncation
warnings under g++ 11 on Linux, 0 under mingw g++ 10. It is still the
only diagnostic wdriver.cpp and wdialog.cpp have ever had (nothing else
compiles them), and it found two real varargs bugs there on its first
run.

**What it caught first is the reason it exists.** `Makefile.win` carried
`-w`. Taking it out did not reveal warnings — it revealed that the build
had not compiled since 2026-08-29, 62 commits earlier, on a C++17
feature mingw g++ 10 rejects at its default `-std`. `-fpermissive`
downgraded the error and `-w` swallowed the message, and three work log
items had listed "Windows builds" among their nets in the meantime. If a
check claims a build compiles, look at the binary's timestamp.

**When the change touches shared core, the two byte-diff harnesses are
the real gate, and they are not pre-commit** — each needs a baseline
binary built from the commit you are changing:

```sh
git worktree add /tmp/base <commit> && make -C /tmp/base -j4
cp /tmp/base/astrolog ./base-astrolog && git worktree remove /tmp/base --force
tools/chart-matrix.sh    ./base-astrolog > old.txt 2>&1   # chart output
tools/chart-matrix.sh    ./astrolog      > new.txt 2>&1
tools/switch-matrix.sh   ./base-astrolog > oldsw.txt 2>&1 # switch surface
tools/switch-matrix.sh   ./astrolog      > newsw.txt 2>&1
tools/graphics-matrix.sh ./base-astrolog > oldg.txt 2>&1  # the drawing code
tools/graphics-matrix.sh ./astrolog      > newg.txt 2>&1
diff old.txt new.txt && diff oldsw.txt newsw.txt \
  && diff oldg.txt newg.txt                              # empty = proven
```

**The graphics matrix is the third surface, added 2026-09-01** (work log
item 148). The other two never draw a picture: the switch matrix renders
nothing at all, and the chart matrix renders only *text* charts, so
xcharts0-2.cpp, xgeneral.cpp, xdevice.cpp and xscreen.cpp had no
differential over them until this one. 224 renders, one checksum each,
about ten seconds. It prints `MISSING` and a count for any render that
produced no file, because an all-erroring harness diffs to zero and reads
exactly like a proof -- its first draft did precisely that for 15 renders,
through a shell variable collision. Run it twice against the *same*
binary before trusting a clean diff: six renders differed that way until
the PostScript writer's `%%Title` stopped carrying a `mktemp` path.

**They cover disjoint surfaces, and assuming otherwise wastes a day.**
The switch matrix prints each run's stderr and the settings file it
saves; it *never renders a chart*, so charts0-3.cpp, intrpret.cpp and the
x*.cpp text paths are invisible to it. The chart matrix is the opposite.
Work log item 143 converted 1,055 formatting calls, most of them in
exactly the code the switch matrix cannot see, and the switch matrix came
back byte-identical over 75,471 lines while proving nothing about them.

Both must run from the repo root with equally short paths (a deep path
truncates the ephemeris path and changes lookups), and neither lets
"now" reach its output, so runs minutes apart still compare byte for
byte. Sabotage one site and re-run before trusting a clean diff: a
harness whose invocations all error still diffs to zero.

## What the suite cannot tell you

It runs *inside* the program from `InteractQt()`, after the window, menus
and first chart are up. That makes it a real test of the real app, and
means two things it cannot do:

- **it cannot test startup**, because startup already happened. That check
  has to be a separate process, and is: the **Startup diagnostics**
  section of `run-qt-tests.sh`.
- **it shares live `us`/`gs`/`gi` state**, so a test that changes a
  setting must put it back.

It also says nothing about **keyboard focus**, which lives in the dialog
handlers rather than the resource, so no audit sees it either.

And it has only recently begun to say anything about **whether the
numbers are right**. The `oracle` group covers planetary longitude
(against the ephemeris library called directly), the sidereal offset, the
built-in Matrix formulas, and house-cusp ordering. Aspects, midpoints,
progressions, eclipses, returns, the atlas and the interpretation text
still have no reference outside this repo: every other harness compares
the program to *itself*, so a wrong answer that has always been wrong
looks exactly like a right one. See REFACTORING.md's T9.
