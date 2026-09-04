# Continuous integration and packaging — the plan

This is the working document for putting Astrolog's builds, its test
suite, its audits and its release artifacts under GitHub Actions on
`origin` (`github.com/nrvate/Astrolog`), and for deciding what a macOS
build of the Qt port would actually cost.

It is written to be **executed nearly mechanically**. Every item says
what to do, how to know it worked, and — the part this project cares
about most — how to prove the check is not vacuous. Phases are ordered by
value per unit of effort, and each is independently shippable: stopping
after Phase 1 leaves the repo better off than it is today.

Like `QT_GUI_PLAN.md` and `REFACTORING.md`, this is a **living document
worked across many sessions**. Refine it until it is boring, then work
it. Findings and incidents accumulate in the work log at the bottom.

---

## Why

`CLAUDE.md` already records the incident this whole document exists to
prevent:

> Its first run found that **`make -f Makefile.win` had not compiled
> since 2026-08-29**, 62 commits, while three work log items listed
> "Windows builds" among their nets — the oracle build was dead and the
> error was `Error 1`, which the old checks did not match.

Sixty-two commits, three work log items claiming a net that wasn't there,
and the behavioural oracle silently absent. That is not a discipline
problem; it is a missing machine. One job that runs `make -f Makefile.win`
on every push would have caught it on the first one.

Second reason, smaller but real: the four differential matrices need a
baseline binary built from the commit you are changing. `QT_TESTING.md`
tells you to do that by hand with `git worktree add`. On a pull request
CI has the base commit for free, which makes the harness cheaper to run
than to skip — and a harness that is cheaper to run gets run.

Third: the slow checks. `warning_audit.py` (~6 min), `tools/asan-sweep.sh`
(minutes), `tools/win-tests.sh` (minutes) are all documented as
"pre-release rather than pre-commit". "Pre-release" is a promise a human
keeps by remembering. A nightly job keeps it by existing. *(That was the
2026-09-01 answer. The 2026-09-04 one is stronger: the slow lane gates
the release, so "pre-release" is enforced by the release itself, and a
promise kept by a schedule turned out to be kept by nobody — see "Where
this stands".)*

## Ground rules

1. **Every job must be falsified once, deliberately, before it is
   trusted.** Break the thing it claims to check, watch it go red, put it
   back. This is `CLAUDE.md`'s standing rule ("a harness proves nothing
   until you sabotage it") applied one level up, and CI is *more*
   vulnerable to it than a local harness, not less: a green check mark is
   a very convincing way to display "I did nothing". Record the
   falsification in the work log with what was broken and what the job
   said.
2. **Match the failure, not a word.** `CLAUDE.md`: a check matching
   `" error "` does not match `Error 1`. In a workflow, `run:` steps fail
   on non-zero exit, so the rule becomes: never pipe a build into
   anything that swallows its exit status (`| tee`, `|| true`,
   `set +e`), and where a step greps output, assert on the *shape* of
   success rather than the absence of a word.
3. **One build definition, used by both CI and release.** Copied from
   swisseph's `package.yml`, and from the reason they give for it: that
   repo had two Windows build definitions and the unbuilt one rotted for
   a decade. Astrolog already has the same crack open — see the
   `Astrolog.vcxproj` finding below.
4. **Windows parity remains the spec.** CI exists to protect the oracle,
   not to replace it. Nothing here changes what any build computes.
5. **No secrets in any workflow** until Phase 10, which is the only phase
   that needs one. Everything before it runs with
   `permissions: contents: read`.
6. **Never push to `upstream`.** The hard rule from `CLAUDE.md` applies to
   anything a workflow could ever be given credentials for. No workflow
   gets write access to anything except, in Phase 5, this fork's own
   releases.

---

## Ground truth: what is verified, what is not

A work document that launders guesses as facts is worse than no document.
Everything in the first table was checked in the tree on 2026-09-01.
Everything in the second is a prediction, and each has an item in the
phases below whose job is to settle it.

### Verified

| Fact | Evidence |
|---|---|
| CI exists as of 2026-09-02 | `.github/workflows/ci.yml`, one job: the two Windows builds and their freshness assertions (PR 1). Everything else in this document is still unwritten |
| The repo is small enough to check out whole | 164 tracked files, `size-pack` 14.47 MiB, `.git` 28 MB (re-counted 2026-09-02; it was 155 the day before, which is the rate these numbers move at) |
| All runtime data is tracked and small | `ephem/` 2.2 MB (12 files), `font/` 2.7 MB; total non-source payload ~5 MB |
| The Windows build is fully static | `Makefile.win`: `-static -static-libgcc -static-libstdc++`; the package is one `.exe` plus data |
| **`Astrolog.vcxproj` is stale and would not link** | it does not list `switch.cpp`, which this fork added in `a78436f`; the project file's last commit is `95fd050`, 2022-04-01, "Upload all version 7.40 files". It targets `v142`, `Win32`/`x86` only |
| Three separate binaries are needed to run everything | `./astrolog` (plain `Makefile`, needs libX11) drives all four matrices and `settings-round-trip.sh`; `./astrolog-qt-test` runs the suite; `./astrolog.exe` is the oracle |
| Build cost is trivial | measured here, `g++ -O` with Qt5 includes: `calc.cpp` 1.75 s, `qtdriver.cpp` 3.29 s, `general.cpp` 1.44 s; 33 objects ⇒ ~60–70 s serial |
| The Qt port uses no Qt6-removed API | no `QRegExp`, `QDesktopWidget`, `qrand`, `setCodec`, `SkipEmptyParts`, `QApplication::desktop()`, `QLinkedList`, `QStringRef`, `toSet()`. It already calls `horizontalAdvance` and already carries `QT_VERSION` guards at 5.12 and 6.5 |
| **The port builds against Qt6 and passes there** | `make qt6` / `make qt6-test` (commit `2582015`, 2026-09-01) build the same sources against a hand-installed Qt6 named by `QT6_PKGCONFIG`; the suite reports 3561/0 against `./astrolog-qt6-test`. This was an open prediction in this document until the morning it was already false. **Consequence: Qt6 is now a fourth build configuration**, compiled only by whoever remembers — see "The three unbuilt configurations", which it joins unless CI builds it |
| **`WCLI` builds, links, and runs a chart under Wine with no display** | 2026-09-02: `astrolog.h`'s X11 guard widened to `&& !defined(WCLI)`, `Makefile.wcli` added (`-DWCLI`, no `-mwindows`, `SRC_WIN` omitted since `wdriver.cpp` and `wdialog.cpp` are `#ifdef WIN` end to end). Builds in 16.2 s at `-j4`, zero warnings, `PE32+ executable (console)`; `wine ./astrolog-wcli.exe -Yi1 ephem -qa 6 15 1990 12:00 0 122W19 47N36 -R1 _X` prints the same chart the Linux console build does, with no display and no window manager |
| **The suite quietly runs fewer assertions on a thinner ephemeris, and only the pass count says so** | the `objsel` group, measured three ways 2026-09-02: `-i nrvate.as` → 39 of 39 bodies, **83 passed / 0 failed**; `-Yi1 ephem` → 19 of 39, **61 / 2**; `-Yi1 /nonexistent-ephem` → **11 of 39**, **53 / 2**. Thirty assertions stop running and nothing announces it; the failure count is pinned at 2 by the Okyrhoe pair either way. Two consequences: gating those two (item 2.3) without also asserting a count buys silence, and **11 bodies resolve with no ephemeris at all**, so any floor below 11 is vacuous |
| **The suite hung, for everyone without `/swe`, and nothing had noticed** | running it the way CI must — `ASTROLOG_QT_EPHEM=minimal ./run-qt-tests.sh -Yi1 ephem` — blocked past a **ten-minute** timeout in the Chart rendering group, on the Moons chart, at **1.9% CPU in `do_poll()`**. Not slow: blocked. `PrintWarningQt()` had put up a modal box about a missing ephemeris file and there was nobody to dismiss it. **40 of the 49 groups never set `SetNoPopupQt(fTrue)`**, so every one was one missing file away from the same hang. Fixed once in the runner rather than in 40 places; that group went from >10 min to **1.16 s**, and the full suite on the bundled set is **3541/0 in 47 s** |
| Nothing in Astrolog's own code is Linux-specific by inclusion | `<malloc.h>` is behind `#ifdef PC` in both `astrolog.h:347` and `placalc.h`; the only other POSIX headers are `<unistd.h>` and `<dirent.h>`. The three `__APPLE__`/`MACOS` hits in the tree are all inside upstream swisseph's `sweodef.h`/`swephexp.h` |
| The QT backend cleanly displaces X11 | `astrolog.h:81`, `#if !defined(QT) && !defined(WIN)`; `Makefile.qt` links no `-lX11` |
| Fonts already resolve for an installed copy | `qtdriver.cpp:2915` tries `applicationDirPath()/font` before `currentPath()/font` |
| Dark-mode detection degrades safely off Linux | `qtdriver.cpp:4310` falls through to `nSchemeNone` when `gsettings`/`gdbus` are absent, and `ApplyColorSchemeQt()` then returns early, leaving the platform palette alone |
| The version macro is upstream's | `astrolog.h:539`, `#define szVersionCore "8.00"` — this fork has no version of its own |
| **The intermittent was two bugs, both found and fixed 2026-09-01** | `173e9f0`: nine hard-coded temp filenames in `qttest.cpp`, so two suites running at once deleted each other's files mid-measurement — and the deleted file *was* the assertion's subject. `21e8d97`: oracle leg 9 called `ChartTransitSearch()`, which prints through `PrintSz` into `is.S`, a stream only `Action()` opens; `putc` into it made glibc free a backup area it never allocated. Verified by `astrolog-4f`: 10 solo runs and two concurrent pairs clean |
| **Every failure-rate number this document quoted was contaminated** | the "7 of 8 at `627480a`, 5 of 8 at `28599d4`" measurements were taken while a second session was also running suites, which is precisely what triggered bug one. Both sessions quoted a rate as a property of a commit when it was a property of who else was on the machine. Retracted, not corrected — there is no true rate to substitute |
| **The suite was intermittently failing, and sometimes crashed** | the long-strings group reports `mode N survives 120-char name and location (0 bytes)` with N varying run to run; five runs on 2026-09-01 gave 0, 3, 1 and 4 failures plus one **segfault** and one **abort** (rc=134). Reproduced at `28599d4`, before any Qt6 work. Run alone the group passes 3/3, so it is an inter-test interaction. **ASan reproduces the failure and reports no memory error at all** |
| That flakiness is not new, and the "rate went up today" theory did not survive measurement | work log item 141 recorded the same `(0 bytes)` signature on 2026-08-31, undiagnosed. Rebuilt and measured 2026-09-01, same recipe, 8 runs each: **`627480a` (start of day) 7 of 8 failed; `28599d4` (end of day) 5 of 8**. The rate did not rise across the day, so the commit suspected of raising it is not indicated. Both samples are n=8 and the difference is inside noise — what they support is "a long-standing, high-rate intermittent", not a direction |
| The abort is rarer than the failure and is not localised | `rc=134` appeared 0 times in each of those 8-run samples, and twice in 3-run samples elsewhere. Nothing here says where it comes from |
| **A count written into prose is stale before the ink dries, this document included** | it asserted 3551 in three places on 2026-09-01 and was wrong by the next morning — the suite prints 3561 at `c3960f7`. `CLAUDE.md` said 3526, `tools/win-tests.sh` says 2777. Four documents, four different numbers, none of them the program's. **The count is not restated here any more**: CI reads it out of the run, and where a count must be *asserted* it belongs in code beside what produces it (item 2.3) |
| **A new file can be absent from its own commit, and only a clean checkout notices** | `tools/line_endings_audit.py` was never staged in `d9c23bb` — the add used `git add $(git diff --name-only)`, which lists tracked modifications only. Three documents cited a standing audit that did not exist for anyone else until `740d149`. A CI job running from `actions/checkout` fails on that immediately; a developer's working tree never will |
| All eight fast audits pass on the tree as it stands | `rc_audit`, `rc_mnemonic_audit`, `rc_field_audit`, `rc_lookup_audit`, `defaults_audit`, `registry_audit`, `line_endings_audit`, `fixture_coverage_audit` — re-run 2026-09-02, all exit 0, and the three generated tables all diff clean. It said "six" on 2026-09-01, before the last two existed |
| The three generated tables are in sync, and are ending-agnostic **as of `d9c23bb`** | all three diffs clean, and each generator against a CR-stripped `astrolog.rc` now produces byte-identical output. **This was not true before `d9c23bb`**: the old `rc_accel.py` and `rc_cmd.py` did `f.read().split('\r\n')` and collapsed on LF input (421 bytes and 0 bytes against a 12,275-byte table); only `rc2qt.py` was always safe. Falsified properly: junk input gives 100 / 0 / 0 bytes, so all three read the path they are given |
| **The minimal ephemeris is already checked in** | `ephem/` holds `sepl_18.se1` (planets), `semo_18.se1` (Moon), `seas_18.se1` (main asteroids incl. Chiron) and 8 individual TNO short files — 2.1 MB, 1800–2400. **12 files and 2.2 MB since 2026-09-02**, when `se52872.se1` was added so the bundled set passes its own suite (item 2.0) |
| **`astrolog.as` already points at it** | `astrolog.as:63`, `-Yi1 "ephem"`. `nrvate.as:46` is what overrides to `/swe` |
| **19 of 39 bodies resolve from the bundled set — measured, not deduced** | `ASTROLOG_QT_TESTS=objsel ./astrolog-qt-test -Yi1 ephem` prints "19 of 39 bodies resolved and matched their listed name"; the same run with `-i nrvate.as` prints "39 of 39". The prediction from `run-qt-tests.sh`'s header and from the file list was exactly right, and is now retired in favour of the observation |
| **The suite does not degrade gracefully on the bundled set — it fails** | that same run is `FAIL: 61 passed, 2 failed`. Both failures are in the Object Selections group: `qttest.cpp:2535` and `:2537` type raw ephemeris number **52872 (Okyrhoe)** and require Lookup Names to resolve it. `/swe` has `se52872.se1`; `ephem/` does not |
| Making the bundled set pass costs 92 KB | `/swe/se52872.se1` is 93,806 bytes. Adding that one file clears both failures. Reaching 39/39 costs **7.9 MB** for 21 files |
| The 20 that do not resolve are cheap in absolute terms, not free | all exist in `/swe` and total **8.0 MB** (7.9 MB with Okyrhoe: 8,320,773 bytes); measured. **No short-file variant exists for any of them** — the `s`-suffixed short files in `ephem/` ship with Astrolog, not with the Swiss set |
| **Windows and Linux also render byte-identical graphics** | 86 of 109 sections identical outright; the other 23 are all output writers, and all but `-XM` reduce to CRLF plus an embedded output path. `-XM` (metafile) differs at byte 33 — unexplained |
| `graphics-matrix.sh` **used to exit 0 with renders missing, and no longer does** | it printed `== $runs renders, $missing produced no file` and set no status from it; seen live at 115 of 224 MISSING with `rc=0`. Fixed 2026-09-01 (work log item 163): it now prints `NOT A VALID BASELINE` and exits 1. **A CI step does not need to parse that line** — an earlier draft of this document told it to, which would have written a parser for a bug that was already gone |
| ~~`graphics-matrix.sh` hard-depends on `nrvate.as` **and on `/swe`**~~ **— retracted 2026-09-02, and it was the argument for option B** | the claim was "115 of 224 renders fail on the bundled ephemeris, so this harness cannot run on `ephem/` alone". Re-measured with `GRAPHICS_MATRIX_CFG="-Yi1 ephem"`: **224 renders, 0 produced no file**, 136 distinct checksums against 146 on `/swe`, and all 448 checksums differ between the two configurations — so the harness runs, discriminates, and is sensitive to the ephemeris. **All four differential matrices work in CI without `/swe`**, which is what Phase 7 needed and what this row said was impossible |
| **Two of the four matrices had no config lever, and one of those hardcoded the maintainer's settings file** | `tools/influence-matrix.sh` ran `-i nrvate.as` inline, so anywhere but that one machine it computed against a different ephemeris than it was written for, silently. It takes `INFLUENCE_MATRIX_CFG` now, the way `graphics-matrix.sh` takes `GRAPHICS_MATRIX_CFG`. `chart-matrix.sh` and `switch-matrix.sh` need no lever — they take their settings from `astrolog.as`, which is tracked and already points at `ephem/` |
| **The Windows and Linux builds compute byte-identical text charts** | full 71-invocation chart matrix, `WCLI` build under Wine vs the Linux console build: **4 differing lines of 6,936**, all of them one diagnostic's path syntax. Before pinning `-z0 0` it was 228, all cascading from one `ST` vs `DT` header |
| **DST autodetection differs between the two builds** | `Transits at: … (ST Zone 8W)` on Windows-under-Wine against `(DT Zone 8W)` on Linux, for 1991-06-15. `TZ=UTC` does not change it — Astrolog uses its own `-z0 Autodetect`. Possibly a Wine artifact; **undetermined, and worth a work log item** |
| **A package runs correctly from any working directory** | binary + `ephem/` + `font/` + `.as` files + `sefstars.txt` + `seorbel.txt` + `astexo.csv` = **8.8 MB, 41 files**; run from `/`, it renders a correct chart. Astrolog resolves its data from the **executable's own directory**, not the cwd |
| A long install path warns, and past some length it **breaks** | at a 93-character package path the warning `Swiss Ephemeris file path longer than 255 characters, so truncated.` appears but the chart is byte-identical, so it looked cosmetic. It is not: under Wine, where the same directory becomes `Z:\tmp\…`, the ephemeris was **not found at all** and Chiron/Ceres/Eris fell to `0Ari00`. Moving to a short path fixed it. Treat the warning as a real signal, and keep CI paths short |
| **Astrolog falls back to Moshier silently, so a Sun-position smoke test proves nothing** | with `ephem/` removed the Sun still reads `24Gem07'46"`; only velocity moves (+0.9551026 → +0.9551014). Chiron/Ceres/Eris go to `0Ari00` and are the correct discriminator (4.5) |
| **`-i <settings file>` runs no tests at all and exits 0** | `ASTROLOG_QT_TESTS=objsel ./astrolog-qt-test -i astrolog.as` produces **zero bytes, exit 0** — with or without `-n`, and with the ephemeris intact. `-i nrvate.as` and `-Yi1 ephem` both run normally. The console build shows the mechanism: `./astrolog -i astrolog.as _X` exits 2 with the version banner, Astrolog's no-chart-specified path. **Use `-Yi1 ephem`; never `-i astrolog.as`** |
| Piped output only flushes at exit | the suite's `printf` output is block-buffered when stdout is not a terminal, so `\| head` on a running binary shows nothing and looks exactly like a hang. This confounded three probes before it was spotted |
| Planetary moons resolve from neither | Phobos, Deimos, Ganymede … read `0Ari00'00"` even with `/swe`; they need `sepm*.se1`, which `/swe` does not have either (`SwissEph file 'sepm9401.se1' not found`) |
| Fixed stars need no ephemeris file | `sefstars.txt` is tracked; Sirius, Vega, Regulus all compute from the bundled set |
| **`astexo.csv` is live and must ship** | read at `charts3.cpp:1792` via `FileOpen(szFileExoCore, …)`, gated on `gs.fAllExo` — the `-XUx` switch and the "Show E&xoplanets" menu item (`qtdriver.cpp:2085`). 1.1 MB |
| **`Astrolog.vcxproj` is exactly one file behind** | diffed against `Makefile.win`'s `SRC`: `switch.cpp` is the only difference, in either direction (32 vs 33). A one-line repair, not a rewrite — though it still targets `v142` and `Win32` only |
| ~~**Three build configurations in this tree are compiled by nothing**~~ **— one left, 2026-09-02** | it was `Astrolog.vcxproj`, `WCLI` (19 sites) and `WSETUP` (10). CI builds the first two now, and each turned out to be **unbuildable** rather than merely stale — which is the argument the row was making, confirmed twice. `WSETUP` is the one left, and says so at its own `#define` |
| The Windows build has no console entry point | `main()` is inside `#ifndef WIN` (`astrolog.cpp:834`); the `WIN` build enters at `WinMain` (`wdriver.cpp:552`). So `astrolog.exe` cannot be smoke-tested non-interactively |
| `-ldl` is not dead code, but it is a no-op on modern Linux | `dladdr()` is genuinely called at `sweph.cpp:271`, under `__GNUC__`. glibc merged libdl into libc at 2.34, so `ldd ./astrolog-qt` shows no libdl at all on this box |
| **Fortify is already live in the ordinary builds** | `nm -D` counts 10 `*_chk` imports in `./astrolog` and 11 in `./astrolog-qt`, `__sprintf_chk` among them. The `-O0` ASan build has **1** — which measures `CLAUDE.md`'s claim that a fortify-detected overflow "cannot fire under it at all" |
| The Qt binary's dependency closure is large | 41 shared libraries, **~70 MB** measured with `du -Lch` over `ldd` output. `Qt5Network` alone drags in `libgssapi_krb5`, `libkrb5`, `libk5crypto`, `libcom_err`, `libkrb5support` |

### Not verified — settle these, don't assume them

*(Every row below was settled by the item it names, by 2026-09-03. Kept
as written because the point of the table was the discipline, not the
rows: the bundled ephemeris resolves 39 of 39 since Q13 option B, Actions
is free on this public fork including macOS, Apple clang compiles the
core, Homebrew's `qt@5` is a source build so macOS uses Qt6, the
menu-role relocation broke nothing, `-ldl` is dropped on macOS, and the
Linux artifact is native packages rather than an AppImage.)*

| Claim | Which item settles it |
|---|---|
| The exact resolvable-body count under the bundled `ephem/` is 19, not 18 or 20 | 2.0 |
| GitHub Actions minutes are free for this public fork, macOS runners included | 0.1 |
| Apple clang compiles the shared core | 9.1 |
| Homebrew still ships a usable `qt@5`; whether an arm64 Qt5 exists at all | 8.3 |
| macOS `QAction::menuRole` relocation does not break the menu-parity tests | 9.3 |
| `-ldl` links on macOS (SDK `libdl.tbd`) or must simply be dropped | 9.1 |
| An AppImage is the right Linux artifact rather than a tarball | 4.2 |

---

## What we are copying from swisseph, and why

Their CI is three workflows, 1374 lines: `ci.yml` (10 jobs), a reusable
`package.yml` (`on: workflow_call`, four platform packages), and
`release.yml` (on `v*` tags). Four of its ideas are worth taking whole,
and each one is documented there as something that already went wrong:

1. **`package.yml` is called by both `ci.yml` and `release.yml`**, so a
   release cannot ship artifacts built by a recipe CI never ran.
2. **Smoke tests assert on output, not exit status** — `swetest` prints
   "illegal option" and carries on, so the first version of that check
   verified nothing. Astrolog has the identical hazard: it reports a bad
   switch and continues.
3. **`ubuntu-22.04`, not `ubuntu-latest`, for anything shipped.** glibc is
   forward- but not backward-compatible; a binary built on 24.04 refuses
   to start on Debian 12. They found it by unpacking a real artifact, not
   by reading the workflow.
4. **Every package verifies itself**: `SHA256SUMS` is generated *and*
   round-trip checked, required files are asserted present, forbidden
   files asserted absent, and the count of covered files is compared
   against the count of files in the package.

Two more of their scars are worth pre-empting rather than re-earning:
`actions/upload-artifact` does not preserve unix modes, so binaries
arrive `0644` and their first release shipped a `swetest` that could not
be executed; and a `SHA256SUMS` written by PowerShell's `Set-Content`
gets CRLF, which makes `sha256sum -c` fail on every line on every
platform.

---

## The build products CI has to produce

| Binary | Makefile | Needs | Used by |
|---|---|---|---|
| `./astrolog` | `Makefile` | `libx11-dev` | `chart-matrix.sh`, `switch-matrix.sh`, `influence-matrix.sh`, `graphics-matrix.sh`, `settings-round-trip.sh` |
| `./astrolog-qt` | `Makefile.qt` | `qtbase5-dev`, `pkg-config` | the shipped Linux app |
| `./astrolog-qt-test` | `Makefile.qt.test` | same | `run-qt-tests.sh`, `QTTEXTDIR`/`QTGRAPHDIR` captures |
| `./astrolog-qt-asan` | `Makefile.qt.asan` | same | (asan-sweep builds its own console binary) |
| `./astrolog.exe` | `Makefile.win` | `g++-mingw-w64-x86-64` | the oracle; the Windows package; `win-tests.sh`, `text-chart-capture.sh` under Wine |
| `./astrolog-wcli.exe` | `Makefile.wcli` | same | the Windows differential (6.4b) — the only Windows binary that runs non-interactively under Wine |

## The three unbuilt configurations

Worth its own heading because it is the failure swisseph's workflow
comments describe, present here three times, and because CI is the only
thing that ever fixes it. A configuration nothing compiles does not stay
still — it rots, silently, and then somebody trusts it.

| Configuration | Sites | Compiled by | State |
|---|---|---|---|
| ~~`Astrolog.vcxproj`~~ | — | **MSBuild, in CI** | **resolved 2026-09-02** — Q11 decided "repair it and build it". It took **three** fixes, and each was something the project relied on the environment supplying: `switch.cpp` missing from the source list (a link error since `a78436f`); `WIN` and `PC` not defined, so `astrolog.h` fell through to `#define X11` and MSVC hunted for `Xlib.h` **on Windows** — the identical bug WCLI had, in the same header; and no `LanguageStandard`, so MSVC compiled as C++14 where `Borrow bciCore(ciCore);` is a hard error, which is the hazard `Makefile.win` documents at length for mingw. `tools/vcxproj_audit.py` keeps the source list honest on Linux in a second |
| ~~`WCLI`~~ | 19 | **`Makefile.wcli`, and CI** | **resolved 2026-09-02** — decided "build it in CI", which is one step in the existing Windows job and no new dependency. Guard widened at `astrolog.h:81`, `-mwindows` dropped, and it runs charts under Wine with no display (1.3b, 6.4b) |
| ~~`WSETUP`~~ | 11 | nothing, deliberately | **resolved 2026-09-03: keep it, unbuilt, commented at every site.** Not deleted (it builds, and it is upstream's code); not built (nothing here needs a Windows installer, and building it means owning eleven sites nobody has run). Every site now says so where a reader stands, which is the failure being guarded against -- finding an `#ifdef WSETUP` block and assuming it works because it is in the tree. **It BUILDS**, 2026-09-03: `-DWSETUP` through `Makefile.win` compiles every site and links once `-lshlwapi` is added for `SHDeleteKey`, giving a 3,218,020-byte binary against the normal build's 2,891,107. So unlike the three rows above it is stale, not broken, and the cost of keeping it is one linker flag. What a link cannot check is whether it *works*: its menu items are Open Directory, Setup User and a registry (un)install, and nothing has ever run them. Windows installer machinery this fork has no use for. The three options remain: build it, delete it, or keep it explicitly unbuilt with a comment at each site so the next reader does not assume it works. What is not an option is leaving it as it is — which is what has happened to it, and to the three rows above, and each of those turned out to be broken in ways nobody could see |
| **Qt6** | — | `make qt6` / `make qt6-test`, by hand | **new on 2026-09-01, and already in this category.** It needs a Qt6 that most machines do not have, so it is outside `make all` by design — which is exactly the shape the other three rows have. A runner can `apt install qt6-base-dev` in seconds, so CI is the *cheapest* place to keep it alive, not the most expensive |

Compare with what *is* built: `WIN` by `Makefile.win`, `QT` by three
makefiles. And note that `Makefile.win` itself spent 62 commits broken
while being described in three work log items as a net — the difference
between "built by something" and "built by something that runs" is the
whole subject of this document.

**This is not a call to adopt all three.** It is a call to decide, once,
per configuration, and write the decision down:

- **Build it in CI** — it becomes real, and it costs a job.
- **Delete it** — the tree gets smaller and nobody is misled.
- **Keep it explicitly unbuilt** — legitimate, but then say so in a
  comment at the site, so the next reader does not assume it works.

The one thing that is not a decision is leaving it as it is.

**`WCLI`: decided 2026-09-02 — build it in CI.** It had a demonstrated
use case rather than a speculative one (it is the cheapest Windows
differential this project has, 6.4b), reviving it cost one line of
shared core, and building it costs one step in a job that already
installs the toolchain. `Makefile.wcli` exists; `.github/workflows/ci.yml`
compiles it. The row above is struck out because the category no longer
holds it.

**`WSETUP`: still undecided**, and the weakest case of the four — Windows
installer machinery this fork has no use for. "Keep it explicitly
unbuilt", with a comment at the site, is the likely answer.

**`Astrolog.vcxproj`: still undecided.** Upstream's, exactly one
`<ClCompile>` line behind, so repairing it is trivial; the real question
is whether MSVC is a configuration this fork wants to owe anything to.

**Qt6: newly undecided, and it should not stay that way for long.** It is
the only one of the four that a contributor can plausibly *depend* on —
somebody on a distribution without `qtbase5-dev` will build `make qt6`
and believe the result. Building it in CI costs one runner-installed
package and about fifteen seconds; the alternative is that its next
regression is found by whoever tries to use it.

---

## Where this stands, 2026-09-04

**Every phase is built and running, and the shape changed on 2026-09-04.**
Five workflows now, every check falsified individually before it was
trusted — with two stated exceptions, macOS and the MSVC build, which
cannot be falsified from a machine with neither and say so in their
files. Five releases are cut and the package repository is live.

| workflow | jobs | what they are |
|---|---|---|
| `ci.yml` (every push and PR, ~5–6 min) | Win32 builds · Qt5 build + suite · Qt6 build + suite · Audits · System install · Behaviour vs base · Which distributions · N × `.deb` (every Ubuntu LTS in standard support, built in containers) · N × `.rpm` (Fedora current + previous, EL9, EL10) · **Windows package** (calls `windows-qt.yml`) | 1.1–1.3b, 2.1–2.3, 3.1–3.4, 4.2–4.6, 6.5, 7.1–7.4, 8.2 |
| `windows-qt.yml` (reusable; called by `ci.yml` and `release.yml`) | Qt 6.8.3 on Windows (MSVC): build, suite, window check, stage · Windows zip and installer, verified under Wine | 4.3, 4.4, Phase 10 |
| `slow-lane.yml` (reusable + dispatch; **no schedule**) | Compiler warnings · Qt6 warning ledger · What nothing executes · ASan over the matrices · UBSan over the matrices and the Qt suite · The Qt suite under ASan · Astrolog against upstream Swiss Ephemeris · Windows parity · MSVC project · macOS build + suite + `.dmg` · The published release, as downloaded (dispatch only) | 6.1–6.4b, 9.x |
| `release.yml` (on `v*`) | Version check · Which distributions · Slow lane · Windows · N × `.deb` · N × `.rpm` · Publish (then retires every release but the newest two) | 5.1, 5.2 |
| `repo.yml` (on release) | Build the repositories (from the remaining releases, for the distributions built today) · Publish to Pages | 4.7 |

**Three decisions, all the maintainer's, all on 2026-09-04:**

1. **The nightly is gone; the slow lane gates the release.** "If things
   are not changing and new releases are not created, no action is
   needed." The nightly had gated nothing, its schedule had fired once
   in its life (every other run was a dispatch), and GitHub disables a
   schedule after 60 quiet days regardless. `release.yml` now calls
   `slow-lane.yml` and its Publish job needs every job in it. A release
   takes about twenty minutes instead of six; the ASan sweep is the long
   pole. The "Behaviour vs yesterday" job was dropped outright — `ci.yml`
   diffs every push against its base, and "since yesterday" means
   nothing without a daily run — and its published-release check became
   a dispatch-only job, because called from a release it would verify
   the *previous* one.

2. **Fedora is asked, not written down.** Both workflows still built
   Fedora 42 — end-of-life since May — and neither built 44, current
   since April. The matrix was defined in four files. It is defined in
   `tools/distros.py` now: the two current releases from Bodhi
   (`state=current`, `id_prefix=FEDORA`, the two highest), with
   endoflife.date as the outage fallback and `FEDORA_RELEASES` as the
   override, EL9/EL10 static in the same file, Ubuntu static because
   those are runner images. Both workflows compute their matrix from it
   through a `Which distributions` job; both repository verifiers read
   their list from it, and a suite the repository still serves but the
   matrix no longer builds is *reported and skipped* rather than
   verified against a container whose mirrors are on their way to the
   archive. Docker Hub's tag list was the first idea and the wrong one:
   it carries 45 and 46 today as prerelease images. **Fedora builds
   against Qt6**, because Fedora will drop Qt5 before EL9 does and a row
   that picks its release automatically should not depend on a library
   that release is retiring; verified in a `fedora:44` container before
   the change was pushed — links Qt 6.11, packages, installs, computes
   Chiron.

3. **Qt is the one interface. The Windows release is the Qt build.** The
   nightly's "experiment" — this port compiled with MSVC against the
   open-source Qt6, `-DQT -DPC` and no `-DWIN` — is `windows-qt.yml`
   now, a reusable workflow `ci.yml` runs on every push and `release.yml`
   runs on a tag. It builds `astrolog.exe` (`/SUBSYSTEM:WINDOWS` with
   `/ENTRY:mainCRTStartup`, so `main()` still runs and no console opens
   beside the GUI; an icon and a VERSIONINFO resource from
   `tools/astrolog-qt.rc`), runs the 3,812-assertion suite on the
   Windows runner under `offscreen`, stages the tree with
   `tools/package-windows-qt.py` (windeployqt's DLLs, the offscreen and
   minimal plugins, the MSVC runtime found through vswhere, the data,
   a LF manifest), audits it, and then does the one thing only that
   machine can: starts it with the real `windows` platform plugin and
   requires a window titled "Astrolog" (`tools/win-qt-starts.ps1`). The
   tree goes to an Ubuntu job that verifies the manifest after the
   round trip, zips it, builds the NSIS installer, and installs and
   uninstalls that under Wine — the proven path, where the same check
   on real Windows would meet UAC. The Win32 build in `wdriver.cpp` is
   still compiled on every push and still driven under Wine by "Windows
   parity", because it is the oracle every divergence is judged against;
   `tools/package.sh` still stages it for the starts-under-Wine check.
   It no longer ships. The honest cost: Windows 10 or later.

   The Linux half of that pipeline — staging, audit, manifest, zip,
   installer, Wine install/uninstall — was run end to end on the
   maintainer's machine before the push, against a stubbed Qt tree and
   the Win32 binary as a stand-in, and both the stager and the audit
   were falsified (a missing plugin, a missing runtime, each refused).
   The Windows half can only be falsified by pushing, and was.

**Signed apt and dnf repositories are live** at
<https://nrvate.github.io/Astrolog/>, rebuilt from every release so old
versions stay installable. **One suite per distribution** — a single
suite makes apt and dnf offer the highest-versioned package rather than
the one built for the running release. Consequence of decision 2: until
the next release, `tools/ci-verify-live-repo.sh` fails on `fc44` — the
repository serves what was published, and nothing has been published
for Fedora 44 yet. That is the check being right.

**Answered along the way:** Q1 (native `.deb` and `.rpm`, not AppImage),
Q2 (`8.00-qt.N`, `szVersionFork`), Q7 (per-change run gates), Q8 (Qt6
kept alive by CI — and EL10 and Fedora depend on it), Q13 (B), Q10
(ad-hoc signed `.dmg`, not notarized), Q11 (the vcxproj is repaired and
built), and item 0.1 (`qt` is the default branch).

**What is left, and none of it is more CI:**

- **`WSETUP`** — the last configuration nothing compiles, and the last
  thing on this list that is purely a decision.
- ~~**Ubuntu 26.04 LTS shipped in April 2026** and the `.deb` matrix still
  says 22.04 and 24.04.~~ **Done 2026-09-04, later**: the `.deb` rows
  build inside `ubuntu:<version>` containers now and are asked of
  Launchpad, so 26.04 is a row and 22.04 retires itself in April 2027.
- **The deeper `-z0 Autodetect` question.** The two builds agree now and
  the unconditional-true bug is gone, but autodetection still answers
  "is it daylight time *now*" rather than "was that date in daylight
  time". Making it date-aware means changing what `DstReal()` resolves
  against everywhere it is used.
- **EL9/EL10 are Rocky images.** Alma is the same ABI and would work.
- **Two build artifacts, 39 MB, are in git history permanently**
  (`5aa4572`, `273a0ac`). Removing them rewrites published history,
  which is the maintainer's call.

**Six shared-core or harness bugs the CI work surfaced**, which is the
argument for having done it at all:

1. The suite hung for ten minutes on any machine without `/swe` — a modal
   box with nobody to dismiss it, in 40 of 49 groups. Fixed.
2. `-z0 Autodetect` was broken in both builds, in opposite directions.
   Fixed.
3. `switch-matrix.sh` had no per-invocation timeout, the only one of the
   four without one. Fixed.
4. `graphics-matrix.sh` could not be run twice concurrently and said so
   nowhere; the damage would have surfaced as a false regression. Guarded.
5. Two unused variables in `xscreen.cpp`'s WCLI block, invisible because
   nothing compiled that configuration. Fixed.
6. The first release refused to publish because both Ubuntu `.deb`s had
   the same filename and one silently overwrote the other. Fixed by
   putting the codename in the version.

## Suggested execution order, and what the first PR contains

Phases are independent enough to reorder, but the value is very unevenly
distributed and the first commit should reflect that.

**PR 1 — "CI: build the Windows oracle" (half a day). Done 2026-09-02,
on branch `qt-ci`.** Phase 0 entire, plus 1.1, 1.2 and — because the
guard fix landed at the same time — 1.3b. One workflow file, one job,
four steps, two committed scripts' worth of logic, and it closes the
failure this whole document was written for. Both checks falsified
locally in a throwaway worktree; see their items for what was broken and
what the build said. **It has not run on GitHub yet**, which is item 0.1
and is the maintainer's to do.

**PR 2 — "CI: the Qt build and its suite" (a day).** Phase 2 entire.
Settle Q13 first because 2.3 depends on it, and expect the mode to be the
bulk of the work rather than the YAML.

**PR 3 — "CI: the audits" (an hour or two).** Phase 3. Nearly free, and
the falsification pass is most of the time.

**PR 4 onwards** — Phases 4, 5, 6, 7 in that order. Packaging before
release, because release calls it; the slow lane and the differentials
last, since both are additive and neither gates anything.

**Phase 8 is optional and conditional**, not part of the run. It is a
prerequisite for Phase 9 *only if* macOS cannot get Qt5, and Phase 9 is
itself scoped to "compiles and passes, for a future adopter". Do not
schedule it; the Linux port stays Qt5.

**Runner cost — measured at HEAD, 2026-09-01, in a clean checkout at
`-j4`.** An earlier draft of this section extrapolated from per-file
compile times and overestimated by roughly 5×. The real numbers:

| target | wall | CPU | output |
|---|---|---|---|
| `Makefile` (console) | 5.0 s | 16.2 s | 1,702,432 b |
| `Makefile.win` | 14.1 s | 51.8 s | 2,888,054 b — **0 errors, 0 warnings** |
| `Makefile.qt` | 14.4 s | 50.0 s | 2,526,104 b |
| `Makefile.qt.test` | 15.3 s | 52.9 s | 2,781,336 b |

**Keep the two numbers apart.** All four together are ~49 s of wall clock
and ~171 s of CPU here. **Billed minutes are the sum** across jobs;
**wall-clock is the longest single job**, since they are independent.
Even at 3× slower on a runner that is under three minutes of billed time
for the entire build matrix — which is why item 1.1 is half a day of work
and why the no-caching rule in Standing hazards costs nothing to keep.
The slow lane is the only genuinely expensive part, and
`warning_audit.py` alone rebuilds all four with `-Wall`.

**Lesson, since it is the same one as everywhere else in this document:**
the estimate and the measurement disagreed by 5×, and the measurement
took 49 seconds.

---

# Phase 0 — Preconditions

Cheap, and two of them can invalidate later phases if left unchecked.

### 0.1 Confirm Actions is available and free on this fork
**Do.** On `github.com/nrvate/Astrolog`: Settings → Actions → General,
confirm Actions are enabled. Check Settings → Billing for included
minutes, specifically whether **macOS** runners are free here — public
repositories historically get standard runners at no cost, but this is
the assumption Phases 9 and 10 rest on and it should be read off the
account, not remembered.
**Verify.** A trivial hand-written workflow with one `run: echo` goes
green.
**Cost.** 10 min.

**Measured 2026-09-02, off the account rather than remembered:**

```
$ gh api repos/nrvate/Astrolog --jq '{private,fork,default_branch,parent}'
{"default_branch":"master","fork":true,"parent":"CruiserOne/Astrolog","private":false}
$ gh api repos/nrvate/Astrolog/actions/permissions
{"enabled":true,"allowed_actions":"all","sha_pinning_required":false}
```

**Actions is already enabled, and there is nothing to click.** A fork
normally arrives with workflows disabled and an "I understand my
workflows, go ahead and enable them" banner on the Actions tab; this one
does not have that state. `allowed_actions: "all"` means the pinned
third-party actions this plan uses are permitted. 0 workflows and 0 runs
so far, which is right — nothing has ever been pushed with a `.github/`
in it.

The repository is **public**, so standard GitHub-hosted runners cost no
minutes. That is the assumption Phase 9 rested on and it holds; Phase 10
is closed anyway.

**But the thing that actually blocks CI is not permissions — it is the
default branch, and it silently disables half of what is written.**

`master` is the default. All work is on `qt`, which is **342 commits
ahead of `master` and zero behind** — a strict fast-forward, no
divergence at all. Consequences, in order of how quietly they bite:

- **`ci.yml` is fine.** Its `push` trigger names `qt`, so it fires on the
  branch work lands on.
- **`nightly.yml`'s `schedule:` will never fire.** GitHub runs scheduled
  workflows only from the **default branch**. A `schedule:` in a file
  that exists only on `qt` is inert, and inert in the quietest possible
  way: no run, no error, no badge.
- **`workflow_dispatch` gets no "Run workflow" button** for the same
  reason — the workflow has to be on the default branch to be dispatched
  from the UI. So the manual escape hatch this document leans on for the
  60-day-inactivity hazard is also absent.
- **Putting the workflows on `master` alone does not fix it either.**
  `master` is upstream's tree from 342 commits ago: no `tools/ci-*.sh`,
  no `Makefile.wcli`, no `ephem/se52872.se1`, no `Makefile.srcs`. Every
  job would fail on a missing file.

**Decided and done, 2026-09-02: `qt` is the default branch.**

```
$ gh api -X PATCH repos/nrvate/Astrolog -f default_branch=qt
{"default_branch":"qt","fork":true,"visibility":"public"}
```

Chosen over fast-forwarding `master` because it is a setting rather than
a history change, and because it makes the repository honest about where
the last 343 commits went. `master` is untouched at `5bf172e` and stays
as the point this fork left upstream; both branches still exist; there
were no open pull requests to retarget.

**One consequence to know about**, since it is the reason the choice was
free: a scheduled workflow now runs from `qt`, and `qt` is where the
work is, so `schedule:` and `workflow_dispatch:` will both behave once
the workflows are on it. They are on `qt-ci`. **Merging that branch is
now the only thing between this document and a real run.**
**Status.** [x] done 2026-09-02 — Actions enabled and free, `qt` is the
default branch, workflows still to merge

### 0.2 Decide the trigger policy
**Do.** Settle, and write into the workflow as a comment: which events,
which branches. swisseph's note is worth reading first — listing a branch
under both `push` and `pull_request` fired both events for every push and
ran all 13 jobs twice, and cancelling one is *not* the fix because the two
events build different commits (`pull_request` builds
`refs/pull/N/merge`, the merge result; `push` builds the squash that
lands).
**Adopted, 2026-09-02, as proposed.** `pull_request:` for everything,
plus `push: branches: [qt, master]`, plus `workflow_dispatch:`, with the
reasoning written into `ci.yml`'s header.
**One fact that was not in the proposal and changes what it means.**
`git log --merges qt` returns **nothing** — this repository has no merge
commits and has never had a pull request. So `push` is the trigger that
fires, `pull_request` is a bet on a future that may not arrive, and
anything designed around a PR's base commit — **Phase 7's differentials,
which are written that way** — has no event to hang on today. Keeping
`pull_request` listed costs nothing; assuming it will fire costs Phase 7.
**Status.** [x] done 2026-09-02

### 0.3 Decide the concurrency and permissions defaults
**Do.** `concurrency: { group: ci-${{ github.workflow }}-${{ github.head_ref || github.ref_name }}, cancel-in-progress: true }` and
`permissions: { contents: read }` at the top of `ci.yml`. Release
workflows opt into `contents: write` separately and never cancel
in-progress.
**Done as specified**, both at the top of `ci.yml`. Two things were added
that this item did not ask for and the next job should keep:
`timeout-minutes` on the job, so a hang costs fifteen minutes rather than
six hours of billed time; and **third-party actions pinned to a commit
SHA rather than a tag** (`actions/checkout@11bd7190…`, v4.2.2, resolved
with `git ls-remote` rather than from memory). A tag is a mutable
dependency, and a document whose whole subject is checks that rot without
saying so should not have one.
**Status.** [x] done 2026-09-02

### 0.4 Confirm nothing in `.gitignore` swallows the workflows
**Verified already** — `.gitignore` covers build outputs, `.idea/`,
`/out/`, editor swap files, `/base-astrolog`. Nothing matches `.github/`.
Note that `/base-astrolog` is already an ignored path, which means the
baseline-binary convention Phase 7 needs is already established here.
**Status.** [x] verified 2026-09-01

---

# Phase 1 — The Windows build compiles

The single highest-value item in this document. If nothing else is ever
done, do this.

### 1.1 Cross-compile `astrolog.exe` on every push
**Goal.** The oracle can never again be dead for 62 commits.
**Do.** Job on `ubuntu-latest`:
`sudo apt-get install -y g++-mingw-w64-x86-64` then
`make -f Makefile.win`.
**Verify.** `make` exits non-zero on failure and the step fails; that is
the whole mechanism. Do **not** pipe it anywhere.
**Falsify.** Introduce a syntax error in `wdriver.cpp` — chosen
deliberately: `wdriver.cpp` and `wdialog.cpp` are compiled by no other
makefile in the tree, so they are the two files this job uniquely
protects. Confirm red, revert with an exact-string reverse patch, **not**
`git checkout` (`CLAUDE.md`'s rule; it reverts the file's whole share of
a change).
**Falsified 2026-09-02**, in a throwaway `git worktree` rather than the
working tree, because a second session was building in it and a
deliberate syntax error would have read as their bug. Dropped the
semicolon after `MSG msg;` at `wdriver.cpp:555`. `make -f Makefile.win`
exited **2**, with four `: error:` lines and
`make: *** [Makefile.win:55: obj-win/wdriver.o] Error 1` — so the step
goes red on exit status alone and nothing has to match a word. Restored
by reverse-patching the exact string; `git diff --stat` then showed the
file untouched.
**Cost.** ~2 min of runner time; 30 min to write and falsify.
**Status.** [x] written and falsified 2026-09-02; has not yet run on
GitHub

### 1.2 Assert the binary is actually new
**Goal.** Close the stale-binary trap at the CI level. `CLAUDE.md`: "if a
check claims to have rebuilt something, look at the binary's timestamp."
**Do.** After the build, a step that fails unless `astrolog.exe` exists
and is newer than every `.cpp` and `.h` in the tree. On a fresh runner
this is trivially true, which is the point — it costs nothing and it
catches the day someone adds caching or an incremental build and the
guarantee quietly disappears.
**Done as `tools/ci-assert-fresh.sh`, not as a workflow step**, so it can
be falsified in a second on a developer's machine instead of minutes at a
time by pushing. That is a general rule this file now keeps — see the
header of `ci.yml`.
**Falsified 2026-09-02, twice, and better than the item asked for.** The
`touch` case is synthetic; the real one is free. After 1.1's sabotage the
failed build left the previous `astrolog.exe` sitting on disk — exactly
the stale binary that read as a pass for 62 commits — and the script
said so:
`NOT FRESH: astrolog.exe is older than the sources it is built from -- ./wdriver.cpp`,
exit 1. With the binary removed entirely it reports
`NOT FRESH: astrolog.exe does not exist -- the build step did not produce it.`
**Status.** [x] written and falsified 2026-09-02

### 1.3 Smoke-test under Wine — Q3
**Goal.** Prove the `.exe` runs, not just that it linked.
**The constraint is confirmed, not suspected.** `main()` sits inside
`#ifndef WIN` (`astrolog.cpp:834`); the `WIN` build enters at `WinMain`
(`wdriver.cpp:552`). There is no console path, so `wine astrolog.exe`
opens a window and sits there, exactly as `win-tests.sh`'s header says.
Three ways out, and they are not exclusive:

- **(a) Don't.** Phase 1 asserts the build links (1.1) and is fresh
  (1.2), and real Windows behaviour waits for Phase 6's Xvfb + Wine
  harness, which already exists and already works. Cheapest, and it loses
  nothing Phase 6 does not recover.
- **(b) Build a second, console Windows binary with `-DWCLI`.**
  `astrolog.h` documents `WCLI` as "a command line Windows version that
  can still popup windows", and because `WCLI` is not `WIN`, such a build
  uses `astrolog.cpp`'s ordinary `main()` — so it is testable
  non-interactively under Wine in one line. It exercises the shared core
  as compiled by mingw for Windows, which is most of what a smoke test is
  for. **It does not exercise `wdriver.cpp`/`wdialog.cpp`**, so it is not
  a substitute for the oracle, and 19 conditional sites of `WCLI` are
  currently compiled by nothing, so *whether it still builds at all is
  unknown*. That makes it a cheap experiment with a real payoff, not a
  plan.
- **(c) Drive the real `.exe` headlessly here**, the way
  `text-chart-capture.sh` does. Correct, and tens of seconds per
  invocation — which is Phase 6's budget, not Phase 1's.
**Recommendation — and (b) was tried, 2026-09-01. It works, and it is
better than expected.**

`WCLI` does **not** build as it stands, and the reason is one line of
this fork's own code. `astrolog.h:81` reads
`#if !defined(QT) && !defined(WIN)` before `#define X11` — `WCLI` is not
in that guard, so a `-DWCLI` build defines `X11` and dies on
`X11/Xlib.h: No such file or directory`. (The failure printed **zero**
`: error:` lines, only `Error 1` — the matcher trap `CLAUDE.md`
describes, encountered live.)

With the guard widened to `&& !defined(WCLI)` and `-mwindows` dropped
from `LDFLAGS`, it builds clean in **14.8 s** and produces a
`PE32+ executable (console)`. Under Wine, with no display, no window
manager and no `xdotool`:

```
$ wine ./astrolog-wcli.exe -qa 6 15 1990 12:00 0 122W19 47N36 -R1 _X
Astrolog 8.00 chart for Fri Jun 15, 1990 12:00pm (ST Zone 0W) …
Sun : 24Gem07   + 0:00' (-) [ 1st house] …
```

**Cost:** a one-line change to shared core plus a `Makefile.wcli` derived
from `Makefile.win`. **Payoff:** see 6.4b — it turns out to be much more
than a smoke test.
**Do not let this hold up 1.1.**
**Done 2026-09-02, and it shipped with 1.1 rather than after it.** The
guard fix is one line — `astrolog.h:81` now reads
`#if !defined(QT) && !defined(WIN) && !defined(WCLI)` — and
`Makefile.wcli` is `Makefile.win` with `-DWIN` → `-DWCLI`, `-mwindows`
dropped, `SRC_WIN` omitted and no resource step. It builds in 16.2 s at
`-j4` with zero warnings, `make wcli` and `make all` reach it, and
`.gitignore` covers its outputs.
**Falsified against the thing it uniquely protects**, which is the point
of building it at all. Dropping the semicolon after
`wndclass.lpfnWndProc = WndProcWCLI;` — `xscreen.cpp:425`, inside
`#ifdef WCLI` — left `make -f Makefile.win` at **rc=0**, blind, and took
`make -f Makefile.wcli` to **rc=2**. There are 33 such sites in the tree
and until 2026-09-02 nothing compiled any of them.
**What is deliberately *not* in the job yet:** running it under Wine.
That is 6.4b, it needs a Wine install on the runner, and it belongs in
the slow lane. The CI step proves it compiles and links; the local
invocation in `Makefile.wcli`'s header proves it runs.
**Status.** [x] done 2026-09-02

---

# Phase 2 — The Qt build and its suite

## The ephemeris problem, and the mode that solves it

`/swe` is a machine-local NAS mount of ~887,000 files. CI will never have
it. But the baseline is **already checked in**: `astrolog.as` sets
`-Yi1 "ephem"`, and `ephem/` holds the planets, the Moon, the main
asteroids and eight TNOs in 2.1 MB. Running CI with `-i astrolog.as`
therefore already exercises a coherent minimal ephemeris. Nothing needs
to be added for the *chart* surface at all.

What is missing is breadth, in exactly one place: 19 of the 39 bodies in
`rgObjSel[]` resolve from the bundled set, and the other 20 skip
themselves. `run-qt-tests.sh` calls that out as the reason it defaults to
`-i nrvate.as`, and the shape of the failure is the one this project
names everywhere else: **a check that quietly tests less is
indistinguishable from a check that passes.**

So the mode is not really "CI mode". What varies is which ephemeris files
are present, not where the build is running — and keying behaviour off
`$CI` would both change behaviour for anyone who happens to have `CI` set
locally and fail to help a contributor without `/swe` who is not in CI.
Name it for the thing that actually varies.

**Four constraints, and the third is the one that matters:**

1. **Declared, not detected.** The mode says which ephemeris the run is
   expected to have; the suite then checks reality against that claim. A
   suite that surveys what is present and adjusts to it can never fail —
   that is the vacuous-harness failure this project has already paid for
   three times (`chart-matrix.sh`'s 15 erroring invocations,
   `switch-matrix.sh`'s 30-of-159 cap, and the two tests found asserting
   an invention).
2. **Exact, not a floor.** `minimal` asserts the count is *equal to* 19;
   `full` asserts *equal to* 39. Not `>= 1`. A threshold picked by
   guessing tests the guess — swisseph hardcoded `>= 100` JNI entry
   points against a real value of 94.
3. **Default is `full`.** The maintainer's local run must be unchanged,
   and a forgotten flag in CI must fail loudly rather than silently
   weaken the net. The failure mode of getting this backwards is a CI
   that goes green while testing half of what it claims — which is worse
   than no CI, because it is believed.
4. **Follow the existing convention.** This tree already drives test
   behaviour from the environment: `ASTROLOG_QT_TESTS=<group>`,
   `ASTROLOG_QT_PROBE=1`, `ASTROLOG_QT_THEME=dark|light`. So:
   `ASTROLOG_QT_EPHEM=minimal|full`, defaulting to `full`.

### 2.0 Decide how much ephemeris CI gets — Q13
**This blocks 2.3 and should be settled before writing the mode.**
Three options, and they are not exclusive:

- **A — minimal only.** Ship nothing new; CI runs `-Yi1 ephem` with
  `ASTROLOG_QT_EPHEM=minimal` and asserts 19. Cost: zero bytes, but the
  two Okyrhoe assertions must then be gated on the mode. Permanent
  consequence: **CI is a weaker net than the maintainer's local run**, so
  a body-specific regression in the other 20 lands green and is caught
  only if someone runs locally with `/swe`.
- **A′ — minimal plus 92 KB.** Add `se52872.se1` alone. The bundled set
  then *passes* with no gating at all, and the mode is left with one job:
  asserting 19. Cheapest thing that makes the suite honest on a runner,
  and the one to do first whatever else is decided.
- **B — check in the missing 20.** 8.0 MB of `.se1`, measured. The pack
  goes from 14.47 MiB to roughly 22 MiB — still a trivial clone. CI then
  covers 39/39 and this particular gap closes permanently. Check the
  redistribution terms first: the repo already ships 11 Swiss data files,
  so the precedent exists, but "upstream already did it" is not a licence
  review.
- **C — fetch them in CI and cache.** Keeps the repo small, keeps 39/39,
  adds a network dependency and something that can rot silently — the
  worst property for a check whose whole job is to not rot.

**Recommendation.** Build the mode either way, because it is what makes
the coverage *stated* instead of assumed, and because it is the lever a
contributor without `/swe` needs. Then take **B** as a separate, cheap
follow-up: 8 MB once buys CI the same net the maintainer has, and A's
permanent asymmetry is the kind of thing that is invisible until it costs
a release.

**A′ taken, 2026-09-02.** `ephem/se52872.se1`, 93,806 bytes, copied from
`/swe`. Measured before and after, same command:

```
ASTROLOG_QT_TESTS=objsel ./run-qt-tests.sh -Yi1 ephem
  before:  19 of 39 bodies resolved …  FAIL: 61 passed, 2 failed
  after:   19 of 39 bodies resolved …  PASS: 63 passed, 0 failed
```

**The resolved-body count did not move**, which is the useful part:
Okyrhoe is typed as a raw ephemeris number by two assertions and is not
one of the 39 rows in `rgObjSel[]`, so **19 remains the number item 2.3
asserts** and A′ removed a special case without introducing one. The
suite on `/swe` is unaffected (3561/0, unchanged).
**On redistribution**, since this item asks for it and it is not settled
by precedent alone: the file is one more Astrodienst `.se1` of the same
kind as the `seas_18.se1` already tracked here, `.gitattributes` already
marks `*.se1` binary, and the repository already ships eleven. That is
the precedent, not a licence review — **B, at 8.0 MB and 20 more files,
is where a real one is owed.**
**Status.** [x] A′ done 2026-09-02; **[x] B done 2026-09-03** — the 20
files measured at 7.8 MB, `ephem/` resolves 39 of 39, and the suite reports
3832/0 under `ASTROLOG_QT_EPHEM=minimal` and against `/swe` alike. CI is
now as strong a net as a local run on this surface.

### 2.0b Measured, 2026-09-01 — and it changed the plan
**Done.** Two of the three predictions this item existed to test were
wrong, and the third was exactly right.

```
ASTROLOG_QT_TESTS=objsel ./astrolog-qt-test -Yi1 ephem
  19 of 39 bodies resolved and matched their listed name
  FAIL: 61 passed, 2 failed
ASTROLOG_QT_TESTS=objsel ./astrolog-qt-test -i nrvate.as
  39 of 39 bodies resolved and matched their listed name
  PASS: 83 passed, 0 failed
```

- **19 was right.** Predicted from `run-qt-tests.sh`'s header and from
  the file list; observed identically. That number can now be asserted.
- **"Degradation, not failure" was wrong.** The suite *fails* on the
  bundled ephemeris: two assertions at `qttest.cpp:2535` and `:2537`,
  which type raw ephemeris number 52872 (Okyrhoe) and require Lookup
  Names to resolve it. Not a program bug — a test that assumes the full
  ephemeris. **`ASTROLOG_QT_EPHEM=minimal` therefore has to handle more
  than the count**, which is exactly what 2.3's survey clause was for.
- **The invocation in this document was wrong**, in the worst possible
  way: `-i astrolog.as` runs **no tests at all and exits 0**. A CI job
  written from the earlier draft of item 2.2 would have gone green
  having executed nothing — the precise failure this document's ground
  rule 1 is about, authored into the document by its own author. Use
  `-Yi1 ephem`.
**Status.** [x] done 2026-09-01

### 2.1 Build both Qt binaries
**Do.** `sudo apt-get install -y qtbase5-dev pkg-config`, then
`make qt -j4` and `make qt-test -j4`.
Keep `-j4` even though the runner's core count differs from this box's
constraint — the cap in `CLAUDE.md` is about not starving the user's
machine, and matching it here keeps one number in one place.
**Done 2026-09-02.** `make qt -j4 && make qt-test -j4`, in the `qt` job.
**Status.** [x]

### 2.1b The suite must be deterministic before anything gates on it
**This blocks 2.2 as a *gate*, though not as a job.**
The suite currently fails intermittently and occasionally aborts (see the
Verified table). **The fix is to fix it, not to accommodate it.**

Stating that because the obvious accommodations are all wrong:

- **Do not retry.** A step that reruns until green is not a gate, it is a
  slower coin flip that also hides the defect. A flaky test is a bug
  report, and CI's job is to deliver it, not to absorb it.
- **Do not run it repeatedly to compute a pass rate.** Same objection,
  plus it multiplies the one number this project actually cares about
  keeping small — CI wall-clock. A single ~60 s run per push is the
  budget; eight of them is not.
- **Do not mark the group `continue-on-error`.** That converts a real
  intermittent defect into a permanently ignored one.

**So:** run it once, let it fail when it fails, and treat each failure as
the bug it is. Until the intermittent is fixed, this job is *informative*
rather than *blocking* — merge on human judgement, not on the badge. That
is a temporary and stated position, not a policy.

**Why this belongs in a CI document at all.** The intermittent has been
present since at least 2026-08-31 and nobody noticed. Measurement since
puts it far higher than item 141's two-in-sixteen — **7 of 8 runs failed
at `627480a`** — which makes the oversight easier to explain and worse:
a check this unreliable was being read as a pass because it was run once,
by hand, at moments that happened to be lucky. CI does not need to retry
to fix that. It needs to run once per push and be believed when it goes
red.

**And it is the sharpest caution in this file about its own method.**
Two sessions measured the same intermittent and drew opposite conclusions
about whether its rate had changed. Both were wrong, and not because
n=8 cannot tell 0.625 from 0.875 — though it cannot. **The measurements
were invalid at the source**: each session's runs were being corrupted by
the other session's runs, through the very bug being measured. A
measurement beats an estimate, which is this document's whole theme; but
a measurement taken in an uncontrolled environment is not evidence, and
two of them agreeing on a shape is not corroboration.

The author of this document also proposed a wrong mechanism from good
data — seeing both members of a concurrent pair crash identically, and
inferring either a shared resource or a time-dependent trigger. Neither.
Both processes ran the same deterministic test and hit the same stream
bug; whether it aborted or segfaulted depended on what glibc found in the
freed backup area. The pairing was an artifact of running pairs.
**Status.** [x] unblocked 2026-09-01 by `173e9f0` and `21e8d97`. The
no-retry position stands on its own and is not contingent on that fix:
had CI been retrying, it would have laundered a temp-file collision
between parallel jobs into a green badge. **CI running suites in parallel
would have found bug one on day one** — and now cannot, which is the
happier version of the same argument.

### 2.2 Run the suite
**Do.** `ASTROLOG_QT_EPHEM=minimal ./run-qt-tests.sh -Yi1 ephem`
(or `-i nrvate.as` with `full`, if Q13 lands on B and the files are in
the tree).
**`-Yi1 ephem`, not `-i astrolog.as`.** Measured in 2.0b: the `-i` form
runs zero tests and exits 0. This document said `-i astrolog.as` for two
drafts.
The script is already headless (`QT_QPA_PLATFORM=offscreen`,
`QT_QPA_PLATFORMTHEME=` cleared, `env -u DISPLAY`) and already exits
non-zero on any failure, so this is one line. It also runs the startup
diagnostics as separate processes, which is exactly the part an
in-process suite cannot reach.
**Falsify.** Break one assertion in `qttest.cpp`; confirm red.
**Done and falsified 2026-09-02.** `Check(cObjSel > 0, ...)` changed to
`> 99999`: the script exited 1, so the step goes red. Restored, rebuilt,
63/0 again.
**And running it this way is what found the hang** — see the Verified
table. The suite had never been run against the bundled ephemeris end to
end, because the maintainer's rule is `-i nrvate.as` and there was no
other reason to. It blocked for ten minutes. **That is the single best
argument in this document for CI existing**: not that it would have
caught a regression, but that it forces the program to be run the way
everyone who is not the maintainer runs it.
**Status.** [x] done 2026-09-02

### 2.3 Build `ASTROLOG_QT_EPHEM` into the suite
**Goal.** Turn "19 of 39 resolved" from a printed diagnostic into an
asserted invariant, under the four constraints above.
**Do.** In `qttest.cpp`, read `ASTROLOG_QT_EPHEM` once at startup into a
mode enum, defaulting to `full`. `TestObjSelTableQt()` currently ends
with `Check(cCheck > 0, ...)` and a `printf` of `cCheck` against
`cObjSel`; replace the floor with an equality against the mode's expected
count. Keep the `printf` — a number in the log is how the next person
learns what changed.
**The two Okyrhoe assertions are already handled**, `qttest.cpp:2535` and
`:2537`. They were the reason the bundled set *failed* rather than
degrading (2.0b); `ephem/se52872.se1` landed 2026-09-02 and the group is
63/0 on the bundled set. **So this item is now only about the count**,
and the count is still 19 — see 2.0.
**Assert the number of assertions too, not only the body count.** The
`objsel` group runs 83 assertions on `/swe`, 63 on `ephem/`, and 53 with
the ephemeris path pointed at nothing — and *reports success* in the last
case except for what the Okyrhoe pair happened to catch, which A′ has now
removed. Thirty assertions can stop running with nothing to show for it.
The body count alone does not cover that; a mode-keyed expected count
does, and it is the same one line.
**Then survey the rest of the suite.** Those two were found by running
it, not by reading it, so assume there are others. Any group whose coverage depends on
ephemeris breadth needs the same treatment or an explicit note that it
does not. The oracle group is the one to check first: `CLAUDE.md`
describes it as 15 bodies over 7 epochs, which the bundled set should
cover completely — confirm that rather than assume it.
**Verify.** `minimal` passes with `-i astrolog.as` and fails with
`-i nrvate.as`; `full` does the opposite. **Both directions**, or the
mode is only half wired.
**Falsify.** Delete one `.se1` from `ephem/` in a scratch checkout: the
count must drop and `minimal` must go red. Then set
`ASTROLOG_QT_EPHEM=full` on a run without `/swe` and confirm it also goes
red — that is the assertion that stops CI silently weakening.
**Done 2026-09-02, and falsified in all four directions plus one the item
did not ask for:**

| run | result |
|---|---|
| `full` + `/swe` | PASS 83/0 |
| `minimal` + `ephem/` | PASS 63/0 |
| `full` + `ephem/` | **FAIL** — "19 of 39 resolved; expects exactly 39" |
| `minimal` + `/swe` | **FAIL** — "39 of 39 resolved; expects exactly 19" |
| unset + `/swe` | PASS 83/0 — the default really is `full` |
| `ASTROLOG_QT_EPHEM=minmal` | **FAIL** before any test runs: a typo is not silently `full` |
| one `.se1` deleted from `ephem/` | **FAIL** — "18 of 39 … ran 18 assertions where it should have run 19" |

**And the count assertion turned out to be the assertion-count assertion,
which this document had as two separate needs.** Every body that fails to
resolve skips its own `Check` silently, so `cCheck` *is* the number of
assertions the loop ran: 83 on `/swe`, 63 on `ephem/`, 53 pointed at
nothing. One equality covers both. Simpler than planned, and the reason is
worth keeping: the two things looked separate until the numbers were put
side by side.
**Status.** [x] done 2026-09-02

### 2.4 Document the mode where a contributor will find it
**Do.** Add `ASTROLOG_QT_EPHEM` to `QT_TESTING.md` beside the other
`ASTROLOG_QT_*` variables, and note in `CLAUDE.md`'s "Always test with
`-i nrvate.as`" rule that the mode is the sanctioned way to run without
it. That hard rule exists precisely because running without `/swe` tests
less; the mode does not weaken it, it makes the weaker run declare
itself.
**Status.** [ ]

### 2.5 Build the plain console binary too
**Goal.** Phase 7 needs it, and it is the only build that compiles the
X11 backend at all.
**Do.** `sudo apt-get install -y libx11-dev`, `make -j4`.
**Done 2026-09-02**, in the `audits` job rather than the `qt` one, since
that is where the round trip and the fortify assertion need it.
**Note.** `tools/asan-sweep.sh` runs `make clean` on both sides of its
overridden build and therefore **deletes `./astrolog` while it runs**. If
a future job ever runs the sweep and the matrices concurrently on one
runner, that is a race. Keep them in separate jobs.
**Status.** [ ]

---

# Phase 3 — The fast audits

Eight standing audits plus three generated-table diffs. All pure Python
or `diff`, all seconds, all currently clean. This is the cheapest phase
in the document and it protects the `.rc`-derived layer that `CLAUDE.md`
says hand transcription got wrong "every time it was used".

### 3.1 Run the fast audits
**Do.** One job, one step each so a failure names itself:
```
python3 tools/rc_audit.py
python3 tools/rc_mnemonic_audit.py
python3 tools/rc_field_audit.py
python3 tools/rc_lookup_audit.py
python3 tools/defaults_audit.py
python3 tools/registry_audit.py
python3 tools/line_endings_audit.py     # fixed in d9c23bb/740d149
python3 tools/fixture_coverage_audit.py # added 2026-09-01; found five
                                        # ranged switches with no
                                        # round-trip fixture line
```
**`line_endings_audit.py` shells out to `git ls-files`**, so it needs a
real working tree — fine under `actions/checkout`, but it exits 1 with a
`CalledProcessError` against a `git archive` extraction. Verified both
ways; in the repo it reports *"line endings clean: N tracked text files,
no carriage returns"* — N was 97 when this was written and is 100 now, so
match on the wording rather than the count.
(`tools/warning_audit.py` belongs to Phase 6 — six minutes, all four
toolchains.)
**Verify.** Each exits 0.
**Falsify.** Each audit needs its own sabotage, because they check
different layers and a single break will not move them all. `CLAUDE.md`
describes exactly what each one catches; break the thing it names, one
at a time.
**All eight falsified, 2026-09-02**, one sabotage each, every one
reverse-patched:

| audit | what was broken | said |
|---|---|---|
| `rc_audit` | renamed both `dxSe_sr` rows in `qtdialog.cpp`, so the table symbol is mentioned nowhere | `Calculation 31 controls` unwired |
| `rc_mnemonic_audit` | `"&File"` → `"F&ile"` in `qtdriver.cpp` | `850 menu labels checked, 1 mismatched` |
| `rc_field_audit` | `dxSe_sr` rewired from `us.fEquator` to `us.fSidereal` | `MISMATCH dxSe_sr windows=us.fEquator qt=us.fSidereal` |
| `rc_lookup_audit` | one row renamed to a symbol no control has | `UNRESOLVED … matches 0 control(s)` |
| `defaults_audit` | one aspect orb changed in `astrolog.as` | `MISMATCH -YAo 5: .as says 5, data.cpp compiles 6` |
| `registry_audit` | a help line's switch changed to `_QQ` | `MISSING charts0.cpp: … "QQ" resolves to no registry row` |
| `line_endings_audit` | one CR into `README.md` | `carriage returns in 1 of 104 tracked text files` |
| `fixture_coverage_audit` | the `-YAo` line removed from the fixture | `ranged settings switches with no fixture line (1 of 17)` |

**Three attempts failed to move `registry_audit` before one did**, and the
reason is a real limit worth writing down: `resolves()` accepts a token if
any *prefix* row matches, because Astrolog's switches take suffix
characters. `H` and `z` are prefix rows, so `_HQQ` and `_zqx` both
"resolve". **The audit cannot catch a typo in the suffix of a prefix
switch** — only a lead character that belongs to no row at all. That is
correct modelling of the parser and a smaller net than the audit's own
description implies.
**And the falsification harness had a bug of its own**, which is the same
lesson one level down: the helper read files in Python text mode, so a
`\r` in a replacement string was normalised to `\n` and the first
`line_endings_audit` sabotage silently became a newline insertion — the
audit "passed" and looked innocent. The same text-mode read then
**converted `astrolog.as` from CRLF to LF** while sabotaging
`defaults_audit`, which is the exact conversion `.gitattributes` forbids
and which moved `switch-matrix.sh` by six lines the last time it happened.
Caught by `git status`, not by any audit — `line_endings_audit` skips
`.as` by design. Restored byte-identical.
**Status.** [x] done and falsified 2026-09-02

### 3.1b The incident that produced that audit — RESOLVED 2026-09-01
**Kept because the failure is instructive, not because it is outstanding.**
Both guards landed in `d9c23bb`, the audit itself in `740d149`, by
`astrolog-4f`. Verified here afterwards: the audit passes, and the 28
binaries are byte-identical to their committed blobs.

On 2026-09-01 the tree-wide CRLF→LF conversion (work log item 159) ran
over the repository's binaries and stripped their carriage returns:
**28 files damaged** — all 11 `ephem/*.se1`, 12 `.ttf`, 4 `.pdf`, both
`.docx` — with bytes lost exactly equal to each file's CR count in every
single case. `ephem/sepl_18.se1` went 484,976 → 481,800 and the program
began answering `Ephemeris file ./ephem/sepl_18.se1 is damaged (0).`
The data was restored from HEAD the same day; all 28 are byte-identical
to their committed blobs again, verified.

**The guards that would stop it recurring are still open** *(as of this
entry; both closed by work log item 159 in `QT_GUI_PLAN.md` on
2026-09-01 — `.gitattributes` names every binary class and
`tools/line_endings_audit.py` exempts them and recognises ELF/PE/Mach-O)*:

1. `.gitattributes` lists only `*.png`, `*.ico`, `*.bmp` as binary. That
   is precisely why `earth.bmp` survived and nothing else did. `*.se1`,
   `*.ttf`, `*.pdf`, `*.docx` are not listed.
2. `tools/line_endings_audit.py` has **no binary exclusion of any kind**.
   Run today it exits 1, flags 39 files, and prints
   `Strip them: tr -d '\r' < FILE > t && mv t FILE`. Of those 39,
   **28 are the binaries that must never be converted** and 11 are
   genuine text files that still carry CRLF (`astrolog.rc` at 3,839 CRs,
   `Astrolog.sln`, the three project files, `astrolog.def`, both `.url`,
   `font/Astro.rtf`, `font/*.txt`, `makefile.com`).

So the audit currently instructs the exact corruption that was just
repaired. Wiring it into CI in that state would automate it.
**How it was resolved.** `.gitattributes` now marks `*.se1 *.ttf *.pdf
*.docx *.rtf *.png *.ico *.bmp` binary, and carries `-text` for the
Windows/VMS tooling (`*.sln *.vcproj *.vcxproj *.rc *.def *.url *.com`)
plus `font/**` and the data files. The audit skips by explicit extension
list rather than by NUL-scanning — which is the right call, because
NUL-scanning the first 8 KB misclassifies `.se1` as text (they open with
an ASCII header) and that mistake was made here first. Its failure
message no longer says "strip them"; it says add the file to SKIP and
`.gitattributes`.
**`astrolog.rc` was deliberately left CRLF** for Visual Studio's resource
editor, alongside the project files. Safe either way — see the generator
note below.
**Note for that conversion — now measured, and it is safe.** `astrolog.rc`
is input to all three generated tables, so item 3.2 is the net for it. I
ran that net ahead of the change: each generator against a CR-stripped
copy of `astrolog.rc` produces **byte-identical** output to the committed
`qtrcdlg.h`, `qtrcaccel.h` and `qtrccmd.h`. The generators already
normalize, which they must — the tables are LF while their input is
CRLF.
**But only three of the seven checks could be tested that way.**
`rc_audit.py`, `rc_mnemonic_audit.py`, `rc_field_audit.py` and
`rc_lookup_audit.py` contain no `sys.argv` at all; they open
`astrolog.rc` directly, by design. Passing them a path is a silent no-op,
and an early version of this note reported them "passing against the LF
copy" when they had read the CRLF original. They have to be re-run after
the file is converted in place.
**That mistake is the ground rule working.** The result looked clean and
was meaningless; a junk-file falsification found it in one command.
**And one claim got corrected rather than defended.** `CLAUDE.md` said
the whole tree was LF; measurement said 11 tracked text files were not.
The documents now read "the source is LF" with the four exempt
categories and a reason for each.
**The live bug this turned up.** Converting `astrolog.as`, `atlas.as`,
`timezone.as` and `nrvate.as` to LF moves six lines of
`switch-matrix.sh` output — `-0q` goes from "Assuming first century C.E.
…" to "Value 0 out of range from 1 to 12", which is a *month*. So the
change is in chart-info parsing, not the atlas lookup. Undiagnosed; the
four files ship unconverted and `.gitattributes` cites this. **A
differential found a real parser bug in data nobody thought of as code.**
**Status.** [x] resolved 2026-09-01 (`d9c23bb`, `740d149`)

### 3.2 Diff the three generated tables
**Do.**
```
python3 tools/rc2qt.py astrolog.rc | diff - qtrcdlg.h
python3 tools/rc_accel.py astrolog.rc | diff - qtrcaccel.h
python3 tools/rc_cmd.py astrolog.rc resource.h | diff - qtrccmd.h
```
**Verify.** `diff` exits non-zero on drift, which fails the step. This is
the pre-commit form; the plain `>` form overwrites the committed file and
must never appear in a workflow.
**Falsify.** Edit one line of `qtrcdlg.h` by hand; confirm red.
**In the `audits` job as of 2026-09-02**, in the pre-commit form.
**Status.** [x]

### 3.3 The settings round trip
**Do.** `tools/settings-round-trip.sh` (needs `./astrolog` from 2.5).
**In the `audits` job as of 2026-09-02.**
**Verified 2026-09-01:** all three legs pass in **0.15 s** — fixed point
after one round trip, all flipped flags persisted, all 31 sentinels
saved. Cheapest check in the document by a wide margin.
**Status.** [ ]

### 3.4 Assert the shipped builds still have fortify
**Goal.** Keep a net that already exists and that nothing currently
guards. `CLAUDE.md` records that work log item 142 "survived 17 gdb runs
and a full ASan sweep because every hunt pointed at a build that
structurally could not see it" — the `-O0` sanitizer build, where
`_FORTIFY_SOURCE` is inactive.
**The good news, measured rather than assumed:** the ordinary builds do
have it. `nm -D ./astrolog` shows 10 `*_chk` imports and
`nm -D ./astrolog-qt` shows 11, `__sprintf_chk` included; the ASan build
shows 1. So the fortify class is covered wherever a normal binary is
exercised — which Phase 7 does, for all four matrices.
**What is unguarded is that it stays true.** Adding `-O0` for a debugging
session, or a distribution changing its compiler defaults, would remove
the net with no diagnostic whatsoever, and the next item-142-shaped bug
would again be hunted in a build that cannot see it.
**Do.** One step: `nm -D ./astrolog | grep -c _chk`, fail below a pinned
count; same for `./astrolog-qt`. Seconds, no new build.
**Falsify.** Rebuild one target with `-O0` and confirm the count
collapses and the step goes red.
**Done as `tools/ci-assert-fortify.sh`, and falsified against the real
thing rather than a synthetic one, 2026-09-02.** `make qt-asan` builds at
`-O0`; the script reports
`FORTIFY GONE: astrolog-qt-asan imports 1 *_chk symbols, expected at least 10`
and exits 1, while `./astrolog` reports 10 and passes. That is
`CLAUDE.md`'s claim about the sanitizer build — that a fortify-detected
overflow "structurally cannot" fire in it — measured, in one command.
**Caveat to write into the step.** This asserts the *mechanism* is
present, not that any particular overflow is caught. It guards a net; it
is not itself one. Written into the script's header.
**Status.** [x] done and falsified 2026-09-02

---

# Phase 4 — Packaging

From here on, follow swisseph's structure: a reusable `package.yml` with
`on: workflow_call`, called by both `ci.yml` and `release.yml`.

### 4.1 Create `package.yml` as a reusable workflow
**Do.** `on: workflow_call`, `permissions: contents: read`. `ci.yml`
gains a `package:` job that is just `uses: ./.github/workflows/package.yml`.
**Rationale.** Ground rule 3. Astrolog already has the crack this closes:
`Astrolog.vcxproj` is a second, unbuilt Windows build definition that has
been rotting since 2022 (it does not list `switch.cpp`).
**Status.** [ ]

### 4.2 The Linux package — decide the form first
**Open question Q1, must be settled before writing this job.** The Qt app
links Qt5 dynamically, so a bare tarball is not runnable on a machine
without Qt. **Measured, so the options can be costed rather than
guessed:** `astrolog-qt` pulls in 41 shared libraries totalling **~70 MB**,
and `Qt5Network` — linked for one feature, the JPL Horizons fetch — alone
brings `libgssapi_krb5`, `libkrb5`, `libk5crypto`, `libcom_err` and
`libkrb5support`.
  - **AppImage** (`linuxdeploy` + `linuxdeploy-plugin-qt`) — one portable
    file, the honest analogue of swisseph's tarball. **~70 MB** for a
    2.5 MB program, most of it ICU and the Qt stack, plus a build-time
    dependency on tooling outside the repo.
  - **Tarball + documented `apt` prerequisite** — trivial to build, and
    it matches what `CLAUDE.md` already tells a developer to install. But
    "download and run" does not work, which is most of what a release is
    for.
  - **`.deb`** — ~8 MB (2.5 MB binary + ~5 MB data), with `Depends:`
    letting apt resolve Qt5. Correct for Debian/Ubuntu/Mint — which is
    the maintainer's own desktop and most of the plausible audience —
    wrong everywhere else, and another format to maintain.
**Worth noting before choosing.** The 70 MB is not a reason to reject
AppImage on its own; it is a reason to know what is being shipped. A
smaller variant exists if `Qt5Network` were made optional, but that is a
feature decision, not a packaging one, and it does not belong in this
document.
**Whichever is chosen:** build on **`ubuntu-22.04`, not `ubuntu-latest`**
(swisseph's glibc scar).

**Done 2026-09-02, and the answer is both native formats rather than any
of the three above.** `.deb` on ubuntu-22.04 and ubuntu-24.04 runners,
`.rpm` in `fedora:42` and `fedora:43` containers. 6.0 MB and 5.9 MB.

**Built on each target, never cross-built** — a package records the Qt and
glibc SONAMEs it linked against, which is the glibc scar generalised.
**Dependencies are computed, never written down**: `dpkg-shlibdeps` and
`rpmbuild` read the ELF `NEEDED` entries, so the `.deb` came out asking
for `libqt5widgets5 (>= 5.2.0~alpha1)`, `libqt5core5a (>= 5.15.1)`,
`libx11-6` and the rest without anyone typing a package name. A
hand-written list is wrong the first time Qt links differently and
silently wrong after that.

**The layout is not FHS-obvious, and the program is why.** Astrolog
resolves its data from the directory of its own executable (item 7.2b
measured what happens otherwise). So the payload goes to
`/usr/lib/astrolog` and `/usr/bin` gets a wrapper that execs it — which
is exactly what `make install` already does, reusing a tested mechanism
rather than inventing one. Symlinks would probably work for the Qt build,
whose `applicationDirPath()` resolves through `/proc/self/exe`, but
"probably" is not a packaging decision.

**The distribution goes in the version.** `.rpm` gets it free from
`%{?dist}` (`.fc42`, `.el9`, `.el10`); a `.deb` has no equivalent, so the
codename is appended: `8.00+qt.1~jammy`, `8.00+qt.1~noble`. `~` sorts
before, so jammy < noble and a 22.04→24.04 upgrade is an upgrade — the
ordering every PPA relies on. **This was found by the release failing**:
without it both Ubuntu builds are `astrolog_8.00+qt.1_amd64.deb`, the
publish job's `merge-multiple` download overwrote one with the other, and
the artifact count came back **6 of 7**. An "at least one artifact" check
would have published a release silently missing a Debian package.
**Verified by installing into a clean image**, not by inspecting the file:
`ubuntu:22.04` and `fedora:42` both install it, resolve Qt from their own
repositories, run it from `/`, and get `Chir: 16Can03`. **Falsified both
arms** — a package with the ephemeris removed reports
`EPHEMERIS NOT FOUND … Chir: 0Ari00`, and one with no `Depends:` is
caught by the unresolved-SONAME check instead.

**One correction to the request that produced this.** Fedora has **no
LTS**; each release is supported about thirteen months, so "the last two
Fedora releases" is a list that moves every six months and lives in the
workflow with a comment saying where to bump it. The genuinely
long-supported RPM targets are the Enterprise Linux rebuilds at EL9/EL10,
which is a separate decision because their Qt5 story is not Fedora's.
**Measured**: Fedora 41, 42 and 43 all still ship `qt5-qtbase-devel`, so
the RPM matches the Debian side on Qt5 rather than diverging to Qt6.
**Status.** [x] done 2026-09-02

### 4.3 The Windows package
**Goal.** The easy one. `Makefile.win` links fully static, so there are no
DLLs to chase.
**Do.** `make -f Makefile.win`, then assemble: `astrolog.exe`, plus the
runtime payload — `ephem/`, `font/`, `astrolog.as`, `atlas.as`,
`timezone.as`, `sefstars.txt`, `seorbel.txt`, `earth.bmp`, `astexo.csv`,
and the documentation (`astrolog.htm`, `changes.htm`, `license.htm`).
**`astexo.csv` ships** — Q4 is settled: `charts3.cpp:1792` reads it for
the `-XUx` exoplanet overlay, reachable from the "Show E&xoplanets" menu
item. Omitting it would give a menu item that silently draws nothing.
**Do not ship** `nrvate.as` (the maintainer's personal settings, pointing
at a NAS mount that exists on one machine), or any source file.
**Verify.** Presence checks for everything required, absence checks for
`nrvate.as` and `*.cpp`, and `SHA256SUMS` round-tripped.
**Done 2026-09-02** as `tools/package.sh windows`: 46 files, 13 MB.
**Ground rule 3 is satisfied by the script, not by a reusable workflow.**
swisseph reaches "one build definition, used by both CI and release" with
`package.yml`; here the definition is `tools/package.sh`, which a release
workflow calls unchanged and which a human can also just run.
**Status.** [x] done 2026-09-02 (Windows only; Q1 still gates Linux)

### 4.4 Self-verify every package
**Do.** For each package, in this order:
  1. assert each required path exists;
  2. assert each forbidden path does not;
  3. generate `SHA256SUMS` (LF, no BOM, trailing newline);
  4. **check it back** — `sha256sum -c`;
  5. assert the number of lines in `SHA256SUMS` equals the file count
     minus one (it does not list itself), so a sums file covering a
     fifth of the package cannot pass.
**Rationale.** Step 5 is not paranoia. This project already shipped a
harness that "was byte-identical over 75,471 lines while proving nothing",
and `switch-matrix.sh` capped its output at 30 lines of 159 for two whole
campaigns. A manifest that covers part of a package is the same failure.
**Done 2026-09-02 as `tools/ci-verify-package.sh`, and step 5 is the
reason there are two scripts instead of one.** The first draft generated
the manifest and then counted its lines — comparing a number against
itself, so it could never fail. Falsification caught it: a manifest
truncated to **9 of 46 lines passed cleanly**. `package.sh` writes the
manifest now and this script only checks it, which is what makes step 5
mean anything.
**All five falsified:**

| broken | result |
|---|---|
| `astexo.csv` removed | `MISSING: astexo.csv` |
| `nrvate.as` smuggled in | `FORBIDDEN: nrvate.as is in the package` |
| a header smuggled in | `FORBIDDEN: source files in the package` |
| a file altered after hashing | `sha256sum: WARNING: 1 computed checksum did NOT match` |
| manifest truncated to 9 of 46 | `sha256sum -c` **passes**; step 5 says `MANIFEST INCOMPLETE` |

That fourth row is the trap in one line: the manifest verified perfectly
and covered a fifth of the package.
**Status.** [x] done and falsified 2026-09-02

### 4.5 Smoke-test the assembled package, not the build tree
**Goal.** Prove the thing a user downloads works, from a directory that is
not the source tree — which is where the `applicationDirPath()/font`
fallback and the ephemeris path resolution actually get tested.
**Do.** Unpack into a temp directory, run the binary **from an unrelated
working directory**, and assert on the output. **Assert on a value, not
on exit status** — Astrolog reports an unknown switch and carries on.

**Verified 2026-09-01, and the obvious assertion turned out to be
vacuous.** A package of the console binary plus `ephem/`, `font/`, the
`.as` files, `sefstars.txt`, `seorbel.txt` and `astexo.csv` — **8.8 MB,
41 files** — runs correctly from `/`, which confirms the layout works.
But then:

```
with    ephem/:  Sun : 24Gem07'46"  … +0.9551026
without ephem/:  Sun : 24Gem07'46"  … +0.9551014
```

**The Sun's displayed position is identical with the ephemeris entirely
absent.** Only the velocity column moves, in the seventh decimal, because
Astrolog silently falls back to the Moshier formulas. A swisseph-style
`^Sun +24Gem07` check therefore passes on a package with **no ephemeris
files in it at all** — the exact failure this item exists to prevent,
and it was the assertion this document specified for two drafts.

**Use a body that cannot compute without the files.** Measured, same
chart, `-R1`:

| body | with `ephem/` | without |
|---|---|---|
| Chiron | `16Can03` | `0Ari00` |
| Ceres | `27Can52` | `0Ari00` |
| Eris | `17Ari31` | `0Ari00` |

`0Ari00'00"` is Astrolog's "no ephemeris" answer — binary, unmistakable,
and exactly what `CLAUDE.md` already documents for a missing `/swe`. Assert
Chiron or Ceres, not the Sun.
**Falsify.** Remove `ephem/` from the package; the step must go red. With
the Sun assertion it stays green, which is how this was found.
**Half of this shipped early, 2026-09-02, applied to `make install`
rather than to a package** — because `make install` is the *other*
"does the shipped thing work" surface, it has the identical data
resolution hazard, and nothing in this document or the tree tested it at
all. `tools/ci-assert-installed.sh` runs the installed wrapper from `/`
and asserts Chiron.
**Falsified as this item specifies**: with `ephem/` moved aside the check
reports `EPHEMERIS NOT FOUND: Chir:  0Ari00 …` and exits 1; restored, it
passes. A real `make install PREFIX=…` was used, not the build tree.
**The Windows package half is still open, and WCLI cannot close it** —
*(superseded 2026-09-04: the Windows package is the Qt build now, which
has `main()`, and `windows-qt.yml` runs the very binary it ships with
`-os` and asserts Chiron on the Windows runner, then installs the
staged tree under Wine and compares it file for file. The strong form
is closed for what ships; what follows is why it could not be closed
for the Win32 build)* —
which was worth finding out, since it looked like the obvious way.
`astrolog.exe` has no console entry point, so the Linux trick of running
the package and asserting Chiron does not transfer; WCLI does have one
and is the same shared core, so it seemed to substitute. It does not.
`SwissEnsurePath()` (`calc.cpp:2579`) branches on `#ifdef WIN`: the real
Windows build asks `GetModuleFileName()` where it lives, and every other
build splits `argv[0]` on `chDirSep` — which `astrolog.h:1564` defines as
`'\\'` whenever `PC` is set, and WCLI sets `PC`. So WCLI's data
resolution is a *third* behaviour, neither the Linux one nor
`astrolog.exe`'s, and a package test built on it would assert something
no shipped binary does. Measured: from inside the assembled package, with
no explicit `-Yi1`, WCLI under Wine reports `Chir: 0Ari00` — which says
nothing about whether `astrolog.exe` would.
**The Windows differential is unaffected**, because it passes `-Yi1`
explicitly on both sides rather than relying on either build finding its
own directory. Re-checked after this: 7,069 lines, identical.
**What would actually close it**: driving the real `astrolog.exe` under
Wine with Xvfb, which is item 6.3's machinery, pointed at an unpacked
package rather than at the build tree.
**Status.** [~] `make install` done and falsified 2026-09-02; the package
smoke test waits on Phase 4

### 4.6 Restore the executable bit
**Goal.** `actions/upload-artifact` does not carry unix modes. swisseph's
first tagged release shipped a `swetest` that answered "Permission
denied".
**Do.** `chmod 0755` the binaries after download, before archiving, and
read the mode back **out of the archive** (`tar tvzf`), not off the disk.
**Done 2026-09-02 as `tools/ci-assert-archive-mode.sh`.** Falsified three
ways: a 0755 member passes, the same package with the binary chmod'd 0644
fails with the offending `tar tvzf` line quoted, and a member that is not
in the archive at all fails rather than passing vacuously.
**The case it guards is the Linux package, which Q1 still blocks** — a
`.zip` carries no unix modes and Windows wants none. Written now because
the mechanism is what rots, and exercised against the Windows tarball
because that is what exists to exercise it on.
**Status.** [x] done and falsified 2026-09-02

### 4.7 Publish apt and dnf repositories
**Not in the original plan**, which stopped at release assets. A user
downloading a file per release is most of packaging undone: the point is
`apt install astrolog` and then upgrades arriving on their own.
**Done 2026-09-02.** `tools/make-repo.sh` builds both from a directory of
packages; `.github/workflows/repo.yml` publishes them to GitHub Pages,
rebuilt from **every** release so old versions stay installable. Live at
<https://nrvate.github.io/Astrolog/>.
**One suite per distribution**, and the first publish is why. It built,
signed, and served every URL at 200 — and could not be installed from: a
single suite makes apt see `~jammy` and `~noble` as two versions of one
package and offer the highest, so Ubuntu 22.04 was handed the noble build
and refused it. dnf had the same fault (`qt.1.fc43` > `qt.1.el9` as
strings). **Nothing short of installing from the live URL would have
shown it.**
**So the workflow installs from what it built, before deploying** —
`tools/ci-verify-repo.sh`, six containers, install and cast a chart,
suites discovered from the tree so a new distribution cannot be silently
left untested. Falsified by recreating the single-suite bug.
**It is the one workflow that needs a secret**, a deliberate exception to
ground rule 5: apt refuses an unsigned repository and dnf needs
`gpgcheck=0`, which is a bad habit to teach in an install instruction.
**Three signing traps, all found by running it**: apt's `signed-by=`
wants a *binary* keyring from a `.gpg` file while dnf's `gpgkey=` wants
the armoured form; dnf's `gpgcheck=1` checks *package* signatures, so the
`.rpm`s must be signed before `createrepo_c` runs, because signing
rewrites them; and rpm execs `%__gpg_sign_cmd`'s first word without
searching `PATH`, so a bare `gpg` reads as "gpg is missing" when it is
installed.
**And the trigger was a no-op.** It fired on `release: published`, but
the release is published by `release.yml` using `GITHUB_TOKEN`, and
GitHub does not start a workflow run from an event that token created.
`workflow_run` on the Release workflow, gated on success, is what
actually fires.
**Status.** [x] done 2026-09-02

### 4.8 Test `make uninstall`
**Also not in the original plan.** It is documented in `README.md` and
`CLAUDE.md` and had never been run by anything — a target nobody executes
is the same category as a configuration nobody compiles, and this
repository has found three of those broken.
**Done 2026-09-02** as `tools/ci-assert-uninstalled.sh`, in the same job
that installs. It checks the removal is complete (a wrapper left on
`PATH` becomes "No such file or directory" months later) and no wider
than it should be (`bin`, `share/applications` and `share/icons/hicolor`
belong to every application; removing the directories shows up as
somebody else's missing menu entry).
**The real one passes** — six files installed, six removed, directories
intact — and the check fails while they are present.
**Status.** [x] done and falsified 2026-09-02

---

# Phase 5 — Release on a tag

### 5.1 Decide this fork's version scheme
**Open question Q2, blocks everything else in this phase.**
`astrolog.h:539` defines `szVersionCore "8.00"` — upstream's number, and
this fork has none of its own. swisseph's release job asserts the tag
matches the source's version, for a reason worth keeping: if they
disagree, every bug report afterwards cites the wrong version.
Astrolog needs something like `8.00-qt.N` in a macro this fork owns, so
the same assertion is possible.
**Status.** [ ]

### 5.2 `release.yml`
**Do.** `on: push: tags: ['v*']` plus `workflow_dispatch` with a tag
input. `permissions: contents: write`. `concurrency` with
`cancel-in-progress: **false**` — a half-published release is worse than
a slow one. Calls `package.yml`, verifies tag vs. version, restores the
exec bit, archives (`.tar.gz` for Linux, `.zip` for Windows), writes a
top-level `SHA256SUMS`, publishes.
**Do also.** Assert the expected number of platform archives is present
before publishing, so a silently-empty package job cannot become a
release with no binaries in it.
**Note.** Tag the *merged* commit, not the branch tip you bumped on —
swisseph shipped a release whose tag was unreachable from `main` and
chose to leave it rather than move a published tag.
**Status.** [ ]

---

# Phase 6 — The slow lane

Nightly (`schedule:`) plus `workflow_dispatch` — **as built on
2026-09-02; since 2026-09-04 `workflow_call` from `release.yml` plus
`workflow_dispatch`, and no schedule.** Everything here is documented as
"pre-release rather than pre-commit"; a release gate is where that
promise becomes mechanical.

### 6.1 `tools/warning_audit.py`
**Do.** All four builds, diffed against `tools/warnings.txt`. Needs
qtbase5-dev, libx11-dev and mingw all present in one job.
**Why it matters here.** Its first run found the 62-commit Windows
breakage, and item 143's sweep left GCC naming every buffer still too
small for its worst case — two of which were live overflows (items
146–147).
**Done 2026-09-02, in `.github/workflows/nightly.yml`.** Three things the
wiring turned up, in descending order of consequence:

- **It cannot run on `ubuntu-latest`.** `tools/warnings.txt` is a ledger
  of what **g++ 11 and mingw g++ 10** say; 24.04 ships g++ 13, which says
  different things about different lines in different words. The audit
  would report hundreds of differences, none of them regressions, and the
  one signal it exists to give would be drowned. The job pins
  `ubuntu-22.04` and `tools/ci-assert-toolchain.sh` says so out loud
  rather than leaving the next reader to work backwards from 300 mystery
  diffs. **That guard found a bug in itself on its first run**: mingw's
  `-dumpversion` answers `10-win32`, so splitting on a dot gave
  `10-win32` and it failed against the very toolchain it was written for.
- **The Qt6 leg cannot run in CI at all**, and correctly skips *(in this
  job; since 2026-09-03 the separate `warnings-qt6` job runs
  `warning_audit.py --qt6-only` on `ubuntu-latest`, where Qt6 has
  pkg-config files, and `ci-assert-slow-lane.sh` tolerates this job's
  skip only while that one is green — see the note under the 2026-09-03
  ledger entry below)*. It needs
  `QT6_PKGCONFIG` to point at a Qt6, and installing `qt6-base-dev` on the
  runner would put Qt6 on pkg-config's default path — where
  `Makefile.qt`'s `pkg-config --exists Qt6Widgets` would find it and
  build the *qt* and *qt-test* legs against Qt6 without saying so.
  `warnings-qt6.txt` also describes Qt 6.8.3 specifically. So the job does
  not install Qt6 and the leg reports `qt6: skipped`; `ci.yml`'s `qt6` job
  still proves that build compiles and passes.
- **It is not six minutes.** Measured 2026-09-02 on this box at `-j4`:
  **1 m 09 s** for all five builds. `CLAUDE.md` and this document both say
  "~6 minutes". At seventy seconds the only thing keeping it out of the
  fast lane is that it needs three toolchains in one job — worth
  revisiting.

**And it has a gap this branch created.** `Makefile.wcli` is a sixth build
and the audit does not know about it. Measured with `-Wall`: **82
warnings against the `win` build's 89** — overlapping heavily, but not
identical, because `-DWCLI` compiles `xscreen.cpp`'s WCLI block that
`-DWIN` does not. Smaller than the category it came from (CI does compile
it, so it cannot rot into not building) but still a build outside the
warning net. Adding it means editing `warning_audit.py` and regenerating
the baseline; left open deliberately rather than done in a hurry beside
another session's edits to that same file.
**Status.** [x] wired 2026-09-02; the `wcli` leg is open *(closed since:
`tools/warning_audit.py` carries a `wcli` build against `Makefile.wcli`
and CLAUDE.md's "all five builds" counts it)*

### 6.2 `tools/asan-sweep.sh`
**Do.** Both surfaces, ~750 invocations.
**Hazards, all documented in the script's own header.** It builds its own
console binary with `-fsanitize=address -DQTTEST`; it runs `make clean` on
both sides of that build and **deletes `./astrolog`**; and four of its
arrangements are load-bearing, including that the binary needs a *short*
path while the output file wants a *long* one. Give it its own job and do
not let anything else on that runner depend on `./astrolog`.
**Known blind spot, now measured and covered elsewhere.** That build is
`-O0`, where `_FORTIFY_SOURCE` is inactive: 1 `*_chk` import against 10
in the ordinary build. Work log item 142 was exactly that class. **Q5 is
settled — no extra build is needed**, because Phase 7's matrices already
run against a normal `-O` binary where fortify is live, and item 3.4
asserts that stays true. Keep this note anyway: the sweep's own output
will never mention the gap.
**Done 2026-09-02, and run in full on the bundled ephemeris rather than
assumed:**

| half | invocations | reported | wall |
|---|---|---|---|
| `switches` | 529 | **0** | 3 m 57 s |
| `graphics` | 229 | **0** | 1 m 03 s |

**The ephemeris path must be absolute**, and this is the same trap as
7.2b from the other end. `asan-sweep.sh` builds its binary at
`/tmp/astrolog-asan-sweep` — deliberately, because the binary needs a
*short* path — and Astrolog resolves a relative `-Yi1` against the
executable's own directory. So `ASAN_SWEEP_CFG="-Yi1 ephem"` there means
`/tmp/ephem`, which does not exist: the sweep would run, report zero, and
leave every ephemeris code path unexercised without a word.
**Status.** [x] done 2026-09-02

### 6.3 `tools/win-tests.sh` under Wine
**Do.** Xvfb + metacity + xdotool + Wine on `ubuntu-latest`.
**Traps, from `QT_COMPARING_WITH_WINDOWS.md`.** Both builds need a window
manager — Qt for its menus to open at all, Wine for a new dialog to
accept keys. xdotool's `--window` path uses `XSendEvent`, which Wine
ignores. Astrolog's accelerators are case-sensitive. The script already
swaps `/swe` for the bundled `ephem/`, because ~887,000 files through
Wine's path translation looks exactly like the app hanging — on CI there
is no `/swe` at all, so **confirm that swap is a no-op rather than an
error** when the mount is simply absent.
**Bell hazard is moot on CI** (`PULSE_SERVER=/nonexistent` matters on the
user's desktop, not a runner) but keep the flag for symmetry with the
documented invocation — and it is already inside `windrive.sh`, which
starts its own Xvfb and its own metacity, so nothing needed adding.
**Done 2026-09-02**, sharing the `windows` nightly job with 6.4b because
installing Wine is the expensive part and paying for it twice is the only
thing that would make either expensive.
**It is not "minutes rather than seconds".** Measured here: **38.9 s** for
both scenarios, passing. `tools/win-tests.sh`'s own header and this
document both say minutes. Third stale number this exercise has turned up,
after the suite's assertion count and the warning audit's six minutes.
**The `/swe` swap is the thing to watch**, and this item was right to
name it. On a runner there is no `/swe`, so `sed 's|"/swe"|"ephem"|'`
matches nothing and the copy is identical to `nrvate.as` — a no-op swap
and a working one look the same from outside. What makes it safe is that
`nrvate.as` on a runner points at a path that does not exist, so the
failure would be loud (every body `0Ari00`) rather than quiet.
**Status.** [x] done 2026-09-02

### 6.4 The Windows-vs-Qt text chart diff
**Goal.** The comparison `QT_COMPARING_WITH_WINDOWS.md` describes, run
automatically for the first time. Both halves run on one Linux runner.
**Do.**
```
make -f Makefile.win && tools/text-chart-capture.sh out/win
make qt-test -j4 && QTTEXTDIR=out/qt ./run-qt-tests.sh
python3 tools/text-chart-diff.py out/win out/qt out/cmp
```
**Q6: what is the pass criterion?** For *this* route — driving the real
GUI binary — still open, because it also captures layout. But 6.4b makes
most of the question moot.
**Status.** [ ]

### 6.4b The cheap Windows differential — no display at all
**This is the find of the verification pass, and it belongs in the fast
lane rather than here.** Using the `WCLI` console build from 1.3b, the
entire 71-invocation chart matrix can be run under Wine with no Xvfb, no
window manager and no input simulation, and diffed against the Linux
console build.

**Measured 2026-09-01, and the answer is essentially zero.**

| run | changed lines of ~6,936 |
|---|---|
| first attempt | 228 |
| with `-z0 0` pinned in the fixture | **4** |

The 4 that remain are two pairs of one diagnostic differing only in path
syntax — `PATH '.;Z:\tmp\w\…'` against `PATH '.;:/tmp/w/…'` — which a
one-line filter removes. **Windows and Linux compute byte-identical text
charts across the whole matrix.**

**What the 224 lines that disappeared were.** A single difference,
cascading: the transit header read `ST Zone 8W` on the Windows side and
`DT Zone 8W` on the Linux side — **standard versus daylight time**. Every
transit listing under it then differed by an arcminute. `TZ=UTC` does
*not* fix it, because Astrolog uses its own `-z0 Autodetect` (set in
`astrolog.as`) rather than the environment. Pinning `-z0 0` does.

**The graphics half too, and it also comes out clean.** Same method with
`graphics-matrix.sh`: 109 renders a side, **86 sections byte-identical
outright**. All 23 that differed were output *writers*, never chart
types — and every one but a single writer resolves to two causes:

- **The Windows build writes CRLF to text output.** For `-Xp`/`-Xp0`
  (PostScript), `-Xbn`/`-Xbc`/`-Xbv`/`-Xba` (XBM), `-XV` and `-X3`, the
  byte delta equals the CR count **exactly** — 9,348 lines of PostScript,
  9,348 bytes.
- **Several formats embed their own output path.** PostScript writes it
  into `%%Title:`, XBM into the C array name (`o_width`, `o_bits`). The
  harness's own header already records this biting it between two runs of
  the *same* binary; across two builds it bites harder, because Wine's
  path is `Z:\tmp\…`.

Hold the output path constant and strip CRs and every one of those is
**identical**. The single exception is **`-XM`, the Windows metafile,
which differs at byte 33** — same total size, unexplained, and the one
writer worth actually looking at.

**Two things not to over-read.** This runs under **Wine**, so the DST
difference may be Wine's rather than real Windows'. And it exercises the
shared core as compiled by mingw — **not** `wdriver.cpp`/`wdialog.cpp`.
It is a complement to 6.4, not a replacement: it answers "does the core
compute the same on Windows", which is most of the value, in 20 seconds
instead of minutes of window driving.

**That live question is answered, and the answer is worse than the
question.** "Is DST autodetection genuinely platform-dependent?" Yes, and
**both implementations are wrong**. Measured 2026-09-02, same chart,
transits at a January date and a July date:

| | January | July |
|---|---|---|
| Linux (console, Qt) | `DT Zone 8W` | `DT Zone 8W` |
| Windows (WIN, WCLI) | `ST Zone 8W` | `ST Zone 8W` |

Neither answers *"was that date in daylight time"*. `general.cpp:2346`
splits the autodetection on `#ifdef PC` into two implementations that can
only agree by coincidence:

- **The PC side** compares `GetSystemTime()` against `GetLocalTime()` and
  sets `is.fDst` from the host machine's **current** clock offset — the
  answer to "is it summer here today", not "was that chart date in
  daylight time".
- **The non-PC side** does the right thing — looks the location up in the
  atlas, consults the timezone-change database — and then **throws the
  result away**: `is.fDst = (dst > 0.0)`, where `dst` is still `dstAuto`,
  which `astrolog.h:567` defines as **24.0**. Unconditionally true.

So every autodetected chart on Linux claims daylight time and every one
on Windows denies it, in January as in July. Unpinned it cascades to 210
differing lines of 7,071, all downstream of one `Transits at:` header,
in the `-Tt` and `-p` sections only.

**Reported, not fixed.** It is a shared-core behaviour change touching
chart output and the maintainer's call, not a CI harness's — and the two
sides need different fixes, so "make them agree" is not one decision.
**This is the second shared-core bug the CI work has surfaced**, after
the suite's ten-minute hang, and it is exactly what item 6.4b was
predicted to be good for.

**Implemented 2026-09-02 as `tools/win-differential.sh`.** 23.6 s, and
better than the prototype: **7,069 lines, zero differences**, where the
2026-09-01 measurement had four residual lines of path syntax. Wired into
the nightly lane — the harness belongs in the fast lane and only the Wine
install keeps it out.

**Three normalisations, each earned by a failure rather than assumed:**

1. **Do not discard Wine's stderr.** The first draft did, and
   "SwissEph file not found" and "The Campanus system of houses is not
   defined at extreme latitudes" promptly appeared on the Linux side and
   nowhere on the Windows side — a harness difference dressed as a parity
   finding. Wine emits no noise of its own here; where it does, filter by
   its tag (`0024:fixme:`), never by dropping the stream.
2. **Diagnostics are compared as a set, chart output as a sequence.**
   `chart-matrix.sh` merges stderr into stdout, and where a stderr line
   lands is a property of the C runtime's buffering — glibc's and
   msvcrt's differ. One line landing before the chart header on Linux and
   after it under Wine was the entire remaining difference across 7,070.
3. **`grep -a`, not `grep`.** Chart output carries IBM line-drawing
   bytes, so grep called the files binary and printed
   `binary file matches` instead of the lines — a trap `QT_TESTING.md`
   already records, met live.

**Falsified three ways**, and the first attempt was wrong in an
instructive direction:

| falsification | result |
|---|---|
| drop `-z0 0` from the **Windows** side only | **identical** — no signal |
| give the Windows side `-s` | **5,961 differing lines of 7,069** |
| drop `-z0 0` from **both** sides | **210 differing lines** |
| change a shared-core format string | identical — both sides move together, as they must |

The first looked like a harness that could not see a one-sided
difference. It is not: the Windows autodetect answer *already equals*
`-z0 0`, so removing the pin from that side changes nothing. Dropping it
from the Linux side is what moves — which is how the direction of the bug
above got established.
**Status.** [x] done 2026-09-02

### 6.5 Graphics renders — move this to the fast lane
**Do.** `QTGRAPHDIR=<dir> ./astrolog-qt-test -i nrvate.as`, offscreen.
**Verified 2026-09-01:** **4.4 s**, 24 PNGs, and **24 distinct
checksums** — no two renders identical, so nothing is silently blank.
At four seconds this does not belong in a nightly lane; put it with
Phase 3. It still needs a pass criterion beyond "24 files appeared"
(see Q6's cousin: a committed checksum baseline would do it, and the
distinctness check above is a decent vacuity guard in the meantime).
**Done 2026-09-02**, in the `qt` job, as
`tools/ci-assert-distinct.sh out/qtg 24`. The count is exact rather than
a floor, for the reason everything else in this document is: a floor
tests the guess. **Falsified three ways** — an empty directory, two
identical files, and the right files against a wrong expected count — each
of which exits 1 and names what it found.
**Still open**: distinctness is a vacuity guard, not a pass criterion. A
committed checksum baseline is what would make this say "correct" rather
than "not blank", and it is the same open question as Q6.
**Status.** [x] done 2026-09-02, with the pass criterion still open

---

# Phase 7 — Differentials against the base commit

The phase that makes CI strictly better than the local workflow rather
than merely more reliable.

### 7.1 Build the baseline
**Goal.** `QT_TESTING.md` tells you to `git worktree add` a baseline by
hand. On a pull request the base commit is already fetched.
**Do.** Check out the merge base into a second directory, `make -j4`
there, and name the result `./base-astrolog` — a path `.gitignore`
already reserves, so the convention is already this repo's.
**Done 2026-09-02 as `tools/ci-differential.sh`**, which does all of
Phase 7 in one script: resolves the base, builds it from a `git archive`
extraction so nothing uncommitted leaks in, copies the binary to
`./base-astrolog` **in the repository root** for 7.2b's reason, runs all
four matrices on both sides with the config levers pinned identically,
diffs, and removes the baseline afterwards. 1 m 42 s end to end on this
box.
**The base commit is not free after all.** `actions/checkout` clones one
commit, so `HEAD~1` is absent; `fetch-depth: 2` covers a single-commit
push and nothing else, and `github.event.before` can be any distance
back. The job uses `fetch-depth: 0` — the whole repository is 14.47 MiB,
which makes full history the cheap answer to a class of failure rather
than a cost. And the base SHA differs by event: `pull_request` wants
`github.event.pull_request.base.sha`, `push` wants `github.event.before`,
which is **all zeros on the first push to a new branch** — the job says
so and passes rather than inventing a baseline.
**Status.** [x] done 2026-09-02

### 7.2 Run all four matrices on both binaries and diff
**Do.**
```
tools/chart-matrix.sh     ./base-astrolog > base-chart.txt 2>&1
tools/chart-matrix.sh     ./astrolog      > new-chart.txt  2>&1
diff base-chart.txt new-chart.txt
```
and the same for `switch-matrix.sh`, `influence-matrix.sh` and
`graphics-matrix.sh`.
**They cover disjoint surfaces and none substitutes for another** — the
switch matrix never renders a chart, the chart matrix renders only text,
and the drawing code is invisible to both. All four, or the coverage
claim is false.
**But not all four in the same lane**, decided 2026-09-02 after the first
CI run measured what they cost. Their per-invocation work differs by an
order of magnitude:

| matrix | invocations (both binaries) | processes each |
|---|---|---|
| chart | 142 | ~2 |
| influence | 48 | ~2 |
| graphics | 448 | ~2 |
| **switch** | **1,058** | **~5** — astrolog, sed, head, grep, rm |

On the runner, the chart matrix **and both builds** finished in 26
seconds while switch was still going when the job was cancelled at 25
minutes. **Nothing was hung** — the console build makes no network calls
and no invocation blocks; a process simply costs far more on a runner
than on an NVMe workstation, and switch runs several thousand of them.

So the pull-request gate runs **chart, influence and graphics** and the
nightly lane runs **all four**. Coverage over a day is unchanged, and
feedback on a change is a minute instead of half an hour. The nightly
one diffs against the commit of 24 hours ago — a weaker question than
"what did this change move", which is why the gate exists as well.
*(The nightly differential was dropped on 2026-09-04 with the schedule.
The switch matrix now runs in CI only inside `tools/coverage-report.sh`,
which executes it rather than diffing it; diffing all four is a by-hand
`tools/ci-differential.sh` against a chosen commit.)*
**Status.** [ ]

### 7.2b What running them actually showed, 2026-09-01
**Done, and it moved two things in this document from claim to
measurement — plus two traps that were not in it at all.**

Baseline: `git archive 22b597f | tar -x` into a scratch directory,
`make -j4` there (5 s), giving a binary three commits behind HEAD.

**All four harnesses are non-vacuous — checked against the specific
failure each one's header records.**

| harness | run | non-vacuity evidence |
|---|---|---|
| `graphics-matrix.sh` | 449 lines, 11 s | **0 MISSING**, 146 distinct checksums over 224 renders |
| `chart-matrix.sh` | 6,936 lines, 5 s | **zero** `Too few parameters` / `Unknown switch` / `Bad parameter` / `illegal` — the shape its own first draft shipped with, 15 of 70 |
| `switch-matrix.sh` | 75,635 lines, 37 s | 530 sections, mean 142 lines, max 168, **zero sections of exactly 30 lines** — the truncation signature that silently capped two whole campaigns. 3 empty and 58 two-line sections are error/usage cases |
| `influence-matrix.sh` | 3,426 lines | runs clean |

**Trap 1 — the baseline binary's path length.** Run from the scratch
directory, the base binary emitted 72 extra lines of
`Swiss Ephemeris file path longer than 255 characters, so truncated.`
The diff was 690 lines, none of it code. This is the same arrangement
`tools/asan-sweep.sh` calls load-bearing: *the binary needs a short
path*.

**Trap 2 — the baseline binary's directory, which is worse.** Moved to
`/tmp/ba`, the warnings vanished and the diff came down to 544 lines of
plausible, alarming numbers: planet longitudes off by one arcminute,
velocities differing in the seventh decimal. All of it false. Astrolog
builds its ephemeris search path from **the executable's own directory**,
so the base binary was searching `/tmp/ephem` — finding nothing and
**falling back to Moshier** while HEAD read the Swiss files. The tell was
one line in the `-8` section:
`PATH '.;:/tmp/;:/tmp/ephem;…'` against `PATH '.;:./;:./ephem;…'`.
A diff of Swiss-vs-Moshier reads exactly like a numerical regression.
**The baseline binary must sit in the repository root** — which is why
`.gitignore` has reserved `/base-astrolog` all along. Copied there, both
matrices diff to **zero**.

**And a coverage statement worth having — since acted on.** With the
baseline placed correctly, the three commits between `22b597f` and HEAD
— 205 lines across `calc.cpp`, `extern.h`, `qttest.cpp` and
`xcharts2.cpp` — produced **zero movement in 71 text charts and 224
renders**. Consistent and probably correct: the house-system fixes
manifest at extreme latitudes, while both matrices pinned one temperate
location. But it meant **the differentials could not have caught a
regression in that work**, and a clean diff there was not coverage.

`astrolog-4f` closed that gap the same day: `chart-matrix.sh` now ends
with six house systems cast at **Longyearbyen, 78N13, in December** — the
latitude where the constructions degenerate and the month when the
sun-declination ones do. Proven rather than assumed: against a
pre-guard binary it moves 166 lines, all inside the new section, with the
6,936 above byte-identical.

**Consequence for this document:** the harness measurements recorded here
are pinned to the commits they were taken at, and `chart-matrix.sh`'s
line count has already moved. Re-measure rather than quote.
**Status.** [x] done 2026-09-01

### 7.3 Falsify all four, once, on purpose
**Non-negotiable, and the most important step in this phase.** A
differential whose invocations all error diffs to zero and reads exactly
like a proof. `chart-matrix.sh` shipped with 15 of 70 invocations
erroring on wrong switch arity; `switch-matrix.sh` capped its output at
30 lines of 159 for two entire campaigns. In CI the same failure is
*less* visible, because nobody watches the log of a green job.
**Do.** For each matrix: change one output-affecting line in the source,
confirm the diff is non-empty and the job is red, reverse-patch the exact
string.
**Falsified 2026-09-02, and it measured the disjointness claim rather
than repeating it.**

| sabotage | chart | switch | influence | graphics | job |
|---|---|---|---|---|---|
| none (control) | — | — | — | — | **rc=0**, 83,918 lines identical |
| `"   Total:"` → `"   TOTAL:"` in `intrpret.cpp` | 12 | 4 | 36 | — | **rc=1** |
| `DrawEdge(x1, y1, …)` → `y1 + 1` in `xcharts1.cpp` | — | — | — | **2** | **rc=1** |

**The second row is the one worth keeping.** One pixel in the drawing
code moved the graphics matrix by two lines and left the other three
**byte-identical over 83,469 lines**. That is work log item 143's blind
spot — 1,055 formatting calls changed, mostly in exactly that code, while
the switch matrix reported byte-identical over 75,471 lines and proved
nothing about them — demonstrated rather than argued.
**A third sabotage refused to apply** (three matches, not one) and the run
came back "no behavioural movement in any of the four" — a clean result
that was really a no-op. It read exactly like a proof, which is the
failure this whole item exists for, and the only thing that caught it was
the helper refusing an ambiguous match.
**Status.** [x] done and falsified 2026-09-02

### 7.4 Decide gate vs. report
**Q7.** A differential answers "something changed", not "something
broke", and `CLAUDE.md` is explicit that it "actively protects a wrong
answer, because fixing a 30-year-old bug shows up as a regression". A
hard gate would make every intentional behaviour change require a
workflow edit. Report-with-diff-attached, and a label or commit-trailer
opt-out for intentional changes, is probably right. Settle before
enabling.
**Settled 2026-09-02: the per-change run gates, the nightly reports.**
That distinction was not in the original answer and the first nightly run
is what produced it — see the note below. A non-empty diff fails the
pull-request job — *unless* a commit between base and HEAD declares it:

```
Behaviour-change: the -0q month range now rejects 0, as it always should have
```

**Tested both ways.** The same one-pixel sabotage that failed the job
passed it once a commit carried the trailer, and the job printed the
declaration back. The reason to gate rather than report: a report is a
green job, and `CLAUDE.md`'s own point about nobody reading the log of a
green job applies hardest here. The reason for the opt-out: a differential
"actively protects a wrong answer, because fixing a 30-year-old bug shows
up as a regression", so an intended change has to cost one line of a
commit message rather than a workflow edit.
**The diffs are uploaded as an artifact whether the job passes or fails**,
because a red differential is exactly when someone wants to read them.

**And the nightly one reports rather than gates** (`DIFFERENTIAL_REPORT=1`)
*(dropped 2026-09-04; the mode stays in the script for by-hand aggregate
runs)*, which the first nightly run argued for on its own. It went red on **122
moved chart lines** that were another session's house-degeneracy fix at
extreme latitudes — correct, intentional, and undeclared only because the
trailer convention was one day old. A gate on a pull request asks "what
did *this* change move" and the author is right there to answer; a
nightly asks "what moved today" across everybody's commits, so failing it
punishes whoever pushed last for someone else's undeclared change. **A
nightly that is red for a legitimate reason is a nightly people stop
reading**, which is the cry-wolf failure — the same disease as a vacuous
check, caught from the other end. Falsified both ways on one sabotage:
gate rc=1, report rc=0, identical diffs.
**Status.** [x] settled and implemented 2026-09-02

---

# Phase 8 — Qt6 — NO LONGER A PROBE, AND NOT A MIGRATION

**Rewritten 2026-09-02. Everything below the first section used to be a
prediction; the tree overtook it in a day.** This phase was written on
2026-09-01 as a conditional prerequisite for a macOS runner, saying that
`qt6-base-dev` was "not installed, not needed" and that whether the port
built against Qt6 was an open question. By the next morning `make qt6`
and `make qt6-test` existed (commit `2582015`), the suite passed 3561/0
against `./astrolog-qt6-test`, and a Qt6-driven fix had landed in
`qtdriver.cpp` (`adfb23c`).

**The position on Linux has not changed and should not: this project is
Qt5.** The port is built, packaged and prescribed against `qtbase5-dev`,
`make all` does not build Qt6, and none of Phases 0–7 wants it. What
changed is not the direction of travel, it is what exists.

**So the question is no longer "build Qt6?" — it is "who keeps the Qt6
build alive?"** A configuration that exists and is compiled by nobody is
the subject of an entire section of this document, and Qt6 walked into
that category on the day it was created. It is also the *easiest* of the
four to keep out of it: `Makefile` reaches Qt6 through `QT6_PKGCONFIG`,
which on this box points at a hand-installed `/usr/local/qt6`, but on a
runner is one `apt-get install qt6-base-dev` and the distribution's own
pkg-config path. That is a fifteen-second job, and it is the concrete
proposal Q8 now asks about.

**What was true and is still worth having recorded:** a grep found no
Qt6-removed API in the port — no `QRegExp`, `QDesktopWidget`, `qrand`,
`setCodec`, `SkipEmptyParts`, `QApplication::desktop()`; it already calls
`horizontalAdvance` and already carries `QT_VERSION` guards at 5.12 and
6.5. The build then confirmed it. **That is worth a note about method:
the grep was right, and it was still not evidence** — the same document
called it "evidence, not a build" at the time, correctly, and the build
is what settled it.

**One long-horizon note, to watch rather than act on.** Qt5 is past
upstream's open-source support, so some future distribution will stop
shipping `qtbase5-dev`. Ubuntu 22.04 LTS is supported for years yet and
24.04 still carries it, so this is not a CI concern now — but if the
packaging target ever moves to a release without Qt5, this phase stops
being conditional. Check the target distribution's Qt5 availability at
that point rather than assuming today's answer.

### 8.1 Build against Qt6 on Linux
**Done, by someone else, before this item was started.** Commit
`2582015`, 2026-09-01. It landed as `qt6` and `qt6-test` targets in the
top `Makefile` that re-enter `Makefile.qt` and `Makefile.qt.test` with
`OBJDIR`/`NAME` overridden and `PKG_CONFIG_PATH` pointed at
`QT6_PKGCONFIG` — which is the shape this item asked for ("parameterize
rather than fork the makefile"), reached without reading it.
**Outcome.** Compiles; `QTTESTBIN=./astrolog-qt6-test ./run-qt-tests.sh`
reports 3561/0. `-std=gnu++17` was indeed required, as predicted here and
for the reason `Makefile.win` documents.
**What is still open is not the build, it is the upkeep** — 8.2.
**Status.** [x] done 2026-09-01 (`2582015`), recorded here 2026-09-02

### 8.2 Decide whether Qt6 is supported, or merely known to work
**Q8, and it is a different question than it was.** The old default
answer — "neither; stay Qt5 and don't build Qt6 at all" — described a
tree where no Qt6 build existed. One does now, and it works, so the
choice is between three positions rather than two:

- **Known to work, kept alive by CI.** One job, `apt-get install
  qt6-base-dev`, `make qt6 && make qt6-test`, run the suite against it.
  Qt5 stays the supported configuration and nothing about packaging
  changes; what CI buys is that the Qt6 build cannot rot silently, which
  is what happens to every other configuration in this tree that nothing
  compiles. **Recommended**, and it is roughly fifteen seconds of runner
  time.
  **And it stopped being insurance on 2026-09-02.** Enterprise Linux 10
  ships **no Qt5** in its own repositories — only via EPEL, which would
  make every user of the EL10 package need a third-party repo for a
  runtime dependency — but it does ship Qt6. So the EL10 `.rpm` is built
  against Qt6, and it is the first thing that actually *depends* on that
  build working. Measured with base + CRB only:

  | | `qt5-qtbase-devel` | `qt6-qtbase-devel` |
  |---|---|---|
  | EL9 | yes | no |
  | EL10 | **no** | **yes** |

  Nothing but the package name changes, because `Makefile.qt` already
  picks its modules from `pkg-config --exists Qt6Widgets`. The EL10
  package links `libQt6Core`, installs and computes Chiron.
- **Known to work, not kept alive.** Honest only if the makefile says so
  at the site: a comment reading "this target is not built by CI and may
  not compile" is a legitimate decision and a silent one is not.
- **Supported.** A matrix dimension forever, and nothing yet demands it.
  Not now.

**A reason the first option is worth more than its cost.** Qt5 is past
upstream's open-source support, so the distribution question is when, not
whether. A working Qt6 build that is *known* to still work is the cheapest
possible insurance against that day, and it is worthless if nobody
discovers it broke six months earlier.
**Decided 2026-09-02: "known to work, kept alive by CI".** The `qt6` job
installs `qt6-base-dev`, builds both Qt6 binaries and runs the suite
against `astrolog-qt6-test`. Qt5 stays the supported and packaged
configuration and nothing about packaging changes.

**Revised the same day by the maintainer, to "both supported, one
build".** The reason given is the one this section already argued and
then under-weighted: Qt5 is past upstream's open-source support, so the
port has to be ready for the day a distribution drops it — while the
maintainer's own machine is Qt5 today and users on it are the point.

So the position is **not** a Qt6 migration and **not** Qt6-as-a-curiosity.
It is what `Makefile.qt` already implements: `QT_MAJOR` asks pkg-config
which Qt is present and builds against the better one, so a single
`make qt` is correct on a Qt5 box and a Qt6 box alike, from the same
sources. Measured 2026-09-02, both give `PASS: 3812 passed, 0 failed`.

What this asks of CI is a second lane rather than a different job: run
the existing checks against each Qt, rather than treating Qt6 as one
build to keep warm. That is the CI author's call to shape.

*And one measurement worth having before that lane exists.* Compiling the
port against Qt6 with `-DQT_DISABLE_DEPRECATED_UP_TO=0x060800` — which
does not warn but *removes* the declarations — succeeds with zero
deprecation diagnostics. Falsified by restoring one
`QMouseEvent::globalPos()` call, which then fails to compile rather than
warning. So the port uses nothing deprecated on the way to Qt 6.8: the
readiness the maintainer asked about is real today, not a plan. It is
deliberately **not** wired into `tools/warning_audit.py` as a gate, since
that would make a Qt6-shaped future the thing the tree optimizes for, and
Qt5 support is a requirement rather than a legacy.

**Two things the implementation turned up that the recommendation did not
anticipate.** The job **must not install `qtbase5-dev`**: `Makefile.qt`
picks its module names from `pkg-config --exists Qt6Widgets`, so a runner
with both installed would build the *Qt5* job against Qt6 and say nothing.
And `QT6_PKGCONFIG`/`QT6_LIBDIR` both exist only for a hand-installed Qt6 —
a distribution's own needs neither — so the `-rpath` is now conditional and
both take an empty override.

**Falsified against a Qt6-only line**, which is the whole point of the job:
`pevent->globalPosition()` at `qtdriver.cpp:273`, inside
`#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)`, renamed. `make qt` stayed
**rc=0** — structurally blind to it — and `make qt6` went **rc=2**. Same
shape as the WCLI falsification, and the same argument.
**Locally verified**, against the hand-installed Qt 6.8.3: builds clean,
suite 3541/0 on the bundled ephemeris. **Not yet verified against a
distribution Qt6**, which is what the runner will have; that is the one
thing in this job a push will settle and this box cannot.
**Status.** [x] decided and implemented 2026-09-02

### 8.3 Establish what Qt is actually obtainable on macOS
**Do.** Check, at the time of doing the work rather than from memory:
whether Homebrew still ships a usable `qt@5`; whether any official
open-source Qt 5.15 arm64 macOS build exists; what
`jurplel/install-qt-action` can still fetch.
**Why this is the crux.** If the answer is "Qt6 or nothing on Apple
silicon", then 8.1 is not optional for macOS, and the two phases merge.
**Status.** [ ]

---

# Phase 9 — macOS: compile and run the suite

**Scope decision, 2026-09-01: this is where macOS work stops.** The
maintainer owns no Apple hardware and cannot run the operating system, so
nothing beyond "it compiles and the assertions pass" can be verified
here, and nothing that cannot be verified should be promised.

That is not a consolation prize. **The deliverable of this phase is
adoptability.** GitHub can build for a platform its owner cannot run, so
a green macOS job is a standing, dated, reproducible statement that the
port compiles and passes its suite on macOS — which is precisely what
someone considering picking up the Mac side needs, and precisely what
they cannot find out for themselves without doing the work first. The job
is the invitation.

**Two consequences to hold onto while working this phase:**

- **Every visual, interactive or platform-integration claim is
  unverifiable by this project** and must be written down as such rather
  than asserted. The suite runs offscreen; it can say the menus were
  built, not that they are usable.
- **The known-unfinished list is a deliverable too**, not a defect log.
  Item 9.3 exists to produce it. An adopter who is handed "here is what
  compiles, here is exactly what nobody has looked at" starts weeks
  ahead of one handed a green check mark and silence.

Can be done entirely on a CI runner without owning a Mac. Depends on 8.3.

### 9.1 Get the shared core to compile under Apple clang
**Expected work, from reading rather than building:**
  - `-std=gnu++17` explicitly (see 8.1).
  - Drop `-ldl` on macOS. **Q9 is settled, and not the way it looked:**
    `dladdr()` is genuinely called (`sweph.cpp:271`, under `__GNUC__`), so
    this is not dead code being pruned. The flag is a no-op on glibc
    ≥ 2.34, which merged libdl into libc — `ldd ./astrolog-qt` shows no
    libdl at all — but it is still needed on older glibc, so **keep it on
    Linux** and drop it only for the macOS link.
  - `Makefile.qt` hardcodes `g++`; on macOS that is a clang alias, so it
    works, but the makefile should take `CXX` from the environment.
**Known-good already.** `<malloc.h>` is behind `#ifdef PC`; the QT
backend displaces X11 by construction; `placalc2.cpp:708` determines byte
order at runtime rather than assuming it.
**Status.** [ ]

### 9.2 Run the suite headless on macOS
**Do.** `QT_QPA_PLATFORM=offscreen` exists on macOS, and
`run-qt-tests.sh` is `/bin/sh` using `env -u DISPLAY`, which BSD `env`
supports. So the existing script should work unchanged.
**Watch.** The theme-detection group. `NSchemeFromKdeQt` and
`NSchemeFromGSettingsQt` have nothing to read on macOS; the gtk-file
tests write into a scratch dir and should still pass. Record which
assertions behave differently rather than deleting them.
**Status.** [ ]

### 9.3 Write the known-unfinished list for whoever adopts the Mac side
**Goal.** This is the phase's real output. Parity with Windows is this
fork's spec, and macOS breaks it in ways nobody here can see, so the
deliverable is an accurate, dated list of what is untouched — filed in
`QT_GUI_PLAN.md`'s divergence list, and marked **unverified by this
project** rather than described as though someone had looked.
**Expected divergences, from reading the Qt documentation, none observed:**
  - Qt maps `Ctrl` to `Cmd` on macOS, so all 264 accelerators shift.
  - `QMenuBar` becomes the global menu bar.
  - `QAction::menuRole` auto-relocates items matching About / Preferences
    / Quit into the application menu, so they leave the menu the Windows
    build puts them in.
  - Fonts, DPI and the offscreen-vs-Cocoa rendering paths are entirely
    unexercised beyond "the assertions passed".
**A prediction worth testing, because it cuts against this whole phase.**
The parity tests walk the `QMenu` object tree, so the 258-item and
264-shortcut checks probably still pass while the *visible* menu
diverges. If that is so, it is a gap in the tests and must be said out
loud: **a green macOS job would then prove the menus were built, not that
a user can find them.** Overstating what the badge means is the one way
this phase could do harm — an adopter who trusts it and discovers
otherwise is worse off than one who was told the truth.
**Status.** [ ]

### 9.4 Bundle layout for data files — deferred to an adopter
**Not needed for this phase**, and recorded here so it is not
rediscovered. A source build run as a plain binary finds its data
already: `qtdriver.cpp:2915` searches `applicationDirPath()/font` before
`currentPath()/font`, and `astrolog.as` points `-Yi1` at `ephem`. The
problem only appears inside a `.app`, where `applicationDirPath()` is
`Foo.app/Contents/MacOS` and the conventional home for data is
`../Resources`.
**When someone does want a bundle**, prefer teaching the existing
two-entry search list a third entry over relocating the data — the list
is already the mechanism, and widening it costs one line and breaks
nothing on Linux or Windows.
**Status.** [~] deferred (see Phase 10)

---

# Phase 10 — macOS: a shippable app — NOT DOING *(reversed: it ships)*

**Superseded 2026-09-03 (Q10, answered differently): every release since
v8.00-qt.3 ships `astrolog-<version>-macos.dmg`, ad-hoc signed and not
notarized, built by `tools/package-macos.sh` — and since 2026-09-04 by
the slow lane's macOS job, after the suite has passed on that binary.
The reasoning below is what was decided on 2026-09-01 and why, kept
because the notarization half of it still holds.**

**Closed 2026-09-01 by the maintainer's decision. Q10 is answered: this
fork does not distribute macOS binaries.** Written down rather than
deleted, because the next person to read this document will have the same
idea, and the reasoning should outlive the decision.

**Why not.** The maintainer owns no Apple hardware. Bundling
(`Info.plist`, `.icns`, `macdeployqt`) is straightforward, but everything
after it is not: an unsigned download is quarantined by Gatekeeper,
ad-hoc `codesign -s -` is enough for a locally built binary and not for
one people download, and real notarization needs an Apple Developer
account (~$99/yr) plus secrets in the repository — the only thing in this
entire document that would break ground rule 5. Paying for and
maintaining a signing identity in order to distribute an artifact nobody
here can launch is the wrong trade.

**What this forecloses, honestly.** There is no "download and run" macOS
build. A Mac user has to build from source, which means installing Qt
themselves. That is a real cost to a real user, and it is the accepted
price of the decision.

**What is left for an adopter**, should one turn up, roughly in order:
bundle layout (9.4), the menu-role and Cmd-key divergences (9.3), an
`.icns`, then signing and notarization under their own identity. Phase 9
is deliberately built so that all of that starts from a green build
rather than from nothing.

**If this is ever reopened**, the cheapest first step is not signing —
it is a Homebrew formula, which puts the build on the user's machine and
sidesteps Gatekeeper entirely without a certificate or a secret.

**Status.** [x] closed, not doing

---

## One Qt backend for all three platforms?

**Answered 2026-09-04 by the maintainer: yes.** Qt is the one interface
on Linux, Windows and macOS; the Windows release is the Qt build
(`windows-qt.yml`), and `wdriver.cpp`/`wdialog.cpp` stay compiled and
driven under Wine as the behavioural oracle only. "Where this stands"
has the shape of it; what follows is the survey and the experiment that
made the decision possible.

Raised by the maintainer on 2026-09-02: if Qt runs on Windows and macOS
as well as Linux, could this tree carry **one** GUI backend instead of
three — `WIN` (`wdriver.cpp`/`wdialog.cpp`), `QT`, and `X11`? "Parity
with Windows" would then stop being something maintained by hand, because
both platforms would be running the same code.

**The survey, done before the experiment rather than after, and it is
shorter than expected.**

| | |
|---|---|
| POSIX dependencies in the Qt backend | **none left.** Three `mkstemp`/`unlink` sites became `QTemporaryFile`; `io.cpp`'s `dirent` use already had an `#ifdef PC` path to `<io.h>` |
| `Q_OBJECT` / `moc` | **none, anywhere.** So no qmake, no CMake, no build system — `cl.exe` over the same source list |
| Linux-specific runtime behaviour | **only dark-mode detection**, and under Qt 6.5+ the code already calls `QStyleHints::colorScheme()`, which is native on Windows and macOS. The gsettings/gdbus/kdeglobals fallbacks simply would not fire |
| Licence | `astrolog.cpp`: GPL "version 2 … or (at your option) any later version", so GPLv3 is available and LGPLv3 Qt is compatible. MSVC plus the open-source Qt is also Qt's own normal combination |
| macOS | measured 2026-09-02: Homebrew pours Qt6 in 59 s and the port **compiles in 61 s** |
| `-DPC`'s switch character | `chSwitch` becomes `/`, but only for **writing**: `io.cpp` uses it to emit settings files and nothing parses with it. Measured — the Linux build accepts `/qa` and casts the chart. So a Windows Qt build would write `/qb` where Linux writes `-qb`, and both read either. No interoperability problem, which is the question that mattered |
| Astrolog's own sources under MSVC | **not clean once MSVC is made conforming** — see below. Clean in MSVC's default mode |

**The one real finding so far, measured 2026-09-02.** Qt's headers demand
`/Zc:__cplusplus` and `/permissive-`, and both messages name the flag, so
each was one line. But `/permissive-` implies `/Zc:strictStrings`, and
under that a string literal is `const char[N]` and will not initialise a
`char *`. MSVC stopped at its 100-errors-per-file cap in three files.

The true scope is not MSVC's to report, so it was measured with the local
compiler instead — the same diagnostic, spelled `-Wwrite-strings`:

| | sites |
|---|---|
| `express.cpp` | 561 |
| `data.cpp` | 415 |
| `atlas.cpp` | 317 |
| Swiss Ephemeris (`sweph`, `swephlib`, `swemplan`, `swedate`, 4 headers) | 45 |
| everything else (`io`, `calc`, `charts0/1/3`, `astrolog.h`) | 7 |
| **total, console build alone** | **1,345** |

**All six makefiles pass `-Wno-write-strings`.** This is not a latent
problem the experiment discovered; it is a suppression the tree has
carried, and the experiment is the first thing to look behind it.

**Then it was done, and the estimate above was wrong.** This paragraph
used to say the sweep was "its own item" and imply weeks. It is **45
added lines**, and the reason is that 1,345 sites were five struct
members and one function signature:

| declaration | where | sites it accounts for |
|---|---|---|
| `CN.szNam` | `atlas.cpp` | 317, across four country/state tables |
| `FUN.szName` | `express.cpp` | 561, the AstroExpression registry |
| `StrLook.sz`, `StrLookR.sz` | `astrolog.h` | the by-name lookup tables |
| `AI.form`, `AI.name` | `astrolog.h` | the Arabic parts formula table |
| `SzClone()`, `PrintAspectsToPoint()` | `extern.h` | both only read their argument |

The ripple everyone feared — "every consumer that assigns from those
tables" — was **four errors in the whole tree**: four locals in
`charts0.cpp` that walk `ai[].form`, and one array in `express.cpp` that
feeds a trie builder already casting to `CONST`.

What is left is **6 sites here and 11 in the vendored Swiss Ephemeris**,
and they are a different problem rather than a remainder: pointers that
genuinely hold a literal *and* a writable buffer at different times.
`ciCore.nam`, `DisplayAtlasLookup`'s `pch1`, and `pes->pchDes` — which
also stores an integer offset in the pointer across a save/restore. Those
want a design decision, not a type change.

So `-Wno-write-strings` stays in the six makefiles for the vendored half,
and `/Zc:strictStrings-` stays in the MSVC job. What changed is that this
tree's own code is conforming now, and `tools/warning_audit.py` holds it
there by *not* passing `-Wno-write-strings` — which is exactly why that
audit owns its flags separately from the makefiles. Baseline 381 → 633
warnings, 95 → 114 sites, all of the growth being the 17 remaining sites
becoming visible instead of hiding in 1,345.

### And then it built

2026-09-02, the `qt-windows` job:

```
built: 2040320 bytes
```

`astrolog-qt.exe` — MSVC, the open-source Qt6, and this port's own
`qtdriver.cpp` and `qtdialog.cpp`, with `-DQT -DPC` and no `-DWIN`. One
`cl.exe` invocation over the same source list, no qmake and no CMake,
because there is no `Q_OBJECT` anywhere to need moc.

**Every obstacle between the first attempt and that line was Qt's opinion
of MSVC's defaults or the runner's layout** — the vcvars path (fixed with
`vswhere`), `/Zc:__cplusplus`, `/permissive-`, `/Zc:strictStrings-`. Not
one was a portability problem in this tree. The single genuine finding,
the const-correctness one above, was a *suppression this tree already
carried*, not something the port broke.

The smoke test that follows the build is a separate story and was wrong
twice; see the work log. The build itself is the result.

### And then it ran, on real Windows

The runner can only say "it compiles and computes a chart". So the job
uploads what it builds, and the artifact went into a Windows 10 VM driven
by `VBoxManage guestcontrol` -- no network needed, the Guest Additions
channel carries files and processes both ways, and `controlvm
screenshotpng` can see the framebuffer without touching the host desktop.

Measured 2026-09-02, ephemeris on a **local disk**:

```
astrolog-qt.exe -Yi1 E:\swe -R1     0.295 s     121 bodies
121 zodiac positions compared against Linux:    0 differences
```

Not "close" -- the same digits, including all 39 esoteric bodies and the
33 planetary moons. The same comparison against the *console mingw* build
on the same VM had already come back at 2 differing lines in 2,812, and
both were the same "file not found" warning differing only in path
separator and in where stdout and stderr interleaved.

### The cost nobody had priced: Qt6 is Windows 10 and later

The first attempt to run it was on Windows 7, and it exited instantly
with no output and no message. Not a port problem:

```
Qt6Core.dll imports SetThreadDescription          (Windows 10 1607+)
Qt6Core.dll imports api-ms-win-core-synch-l1-2-0
```

Both static, so the loader fails before `main()`. Qt 6 dropped Windows 7
at 6.0. **And the obvious check is worse than useless**: the PE header
says minimum OS version **6.0**, which is Vista, and would tell you the
binary is fine. Only the import table is honest.

A Qt5 leg was added to cover Windows 7 and removed within the hour. Qt
5.15.2 -- the last open source 5.15, dated 2020 -- does not compile
against a current MSVC at all: `qvarlengtharray.h` uses
`stdext::checked_array_iterator`, a Microsoft STL extension now behind an
opt-in macro, so Qt's own header fails before any Astrolog source is
reached. One `/D` fixes it; a workaround on a dead Qt only earns more
workarounds.

So the honest statement of the trade, which belongs beside every argument
for collapsing the backends:

| | runs on |
|---|---|
| `wdriver.cpp` (Win32) | Windows 7, 8, 10, 11 |
| this port, Qt6 | **Windows 10 and later** |

Qt5 remains supported on **Linux**, where it is what the maintainer's
machine has and what distributions ship. macOS is Qt6 already, Homebrew
having deprecated `qt@5` into a source build.

### And then it found a real one

Everything above was Qt's opinion of MSVC's defaults. On 2026-09-02 the
job earned its place properly, the first time it complained about this
tree rather than about a compiler flag:

```
qttest.cpp(68): fatal error C1083: Cannot open include file: 'unistd.h'
```

`qtdriver.cpp` and `qtdialog.cpp` shed `<unistd.h>` when three
`mkstemp`/`unlink` sites became `QTemporaryFile`. `qttest.cpp` kept it,
and nothing noticed, because no Windows build had ever compiled the
suite. Separating the Qt methods from the POSIX calls -- 19 `close()` and
1 `write()` are `pw->close()` and `QFile::write()` -- leaves:

| | |
|---|---|
| 15 | `getpid()` |
| 11 | `getenv("TMPDIR") != NULL ? … : "/tmp"` |
| 1 | `unlink()` |

Now `QCoreApplication::applicationPid()`, `QDir::tempPath()` and
`QFile::remove()`. The `"/tmp"` fallback is worth singling out: it is not
merely unportable, it is *wrong* on Windows, and would have written into
`\tmp` on whatever drive happened to be current.

**What found it is the point.** Four sanitizer sweeps, nine audits and a
warning ledger across five builds never mentioned `<unistd.h>`, because
on Linux it is correct. A second platform is a different kind of net from
a stricter tool on the same one, and this is the evidence for that claim
rather than the assertion of it.

A sweep of every other source the Qt/Windows build compiles found two
more POSIX headers, both already guarded: `io.cpp`'s `<dirent.h>` behind
`#ifdef PC` (to `<io.h>`), and `sweph.cpp`'s `<dlfcn.h>` behind
`#ifdef __GNUC__`.

### And then the whole suite passed

2026-09-03, in a Windows 10 VM, against the artifact the nightly builds:

```
== w10test: PASS: 3812 passed, 0 failed
```

The same 3,812 as Linux. 25 dialogs opened and closed with the right
titles, 42 context menus resolved, 264 shortcuts bound and unique, 338
menu items fired, the numeric oracle's 307 assertions against the Swiss
Ephemeris library -- all of it, on Windows, in a build with no `WIN` in
it.

**Getting there took four runs and fifteen failures, and not one of them
was the port.** Worth listing, because the ratio is the point:

| run | failures | what they were |
|---|---|---|
| 1 | 15 | 8 no icon in the artifact, 6 a redirected `HOME` Windows ignores, 1 a comparison carrying a literal `\n` |
| 2 | 6 | the same 6 -- `USERPROFILE` was the wrong guess too |
| 3 | 0 | `SetHomeTestQt()`, a seam instead of a third guess |

The `\n` one is the keeper: that assertion had been testing "the line
ends in exactly `\n`" for as long as it existed, on the only platform
where it ever ran. Three were defects in the test, one in the packaging,
and the port itself never moved.

**What the green result does not settle**, and this is the part worth
keeping in front of the enthusiasm:

- **`astrolog.rc` is a build input, not legacy.** `qtrcdlg.h` (1,903
  lines), `qtrcaccel.h` and `qtrccmd.h` are generated from it, and four
  of the ten audits check the port against it. The Qt dialogs are
  *derived from* the Windows resource script.
- **`wdriver.cpp`/`wdialog.cpp` are the oracle.** Every divergence in
  this fork has been judged against them, and `tools/win-differential.sh`
  and `tools/win-tests.sh` both measure against that build.
- **Windows users would get the Qt look, not native Win32.** That is a
  visible divergence from upstream, and this fork's spec is currently to
  match upstream.
- **It has not been shown to *run*.** The job builds and links; the step
  after it, which asks the binary to compute a chart, has not passed yet.
  A link is not an execution, and this project has been caught by that
  distinction before — which is why the job asserts Chiron rather than
  the Sun, and why it asserts anything at all instead of stopping at
  "built".
- **No dialog has been opened on Windows.** Compiling `qtdialog.cpp` says
  nothing about whether 25 dialogs lay out correctly under a Windows
  style, and the suite has never run there.

**What it would buy, and it is substantial**: the suite's 3,812
assertions — 25 dialogs, 42 context menus, 264 shortcuts — would run
against the actual Windows UI, where today `tools/win-tests.sh` has two
scenarios.

## Standing hazards

Collected so they do not have to be rediscovered inside a workflow, where
rediscovery is slowest.

- **A green job that did nothing looks exactly like a green job that
  worked.** Ground rule 1 exists for this. Every job, falsified once.
- **CI adds no logic.** Every step is a package install, a `make`, or one
  committed script from `tools/`. A check that lives only inside YAML can
  only be falsified by pushing — minutes a try, in a place where nobody
  reads the log of a green job — and ground rule 1 then quietly stops
  being affordable. Item 1.2 is `tools/ci-assert-fresh.sh` for exactly
  this reason, and it was falsified twice in about a second.
- **There are no pull requests in this repository.** `git log --merges qt`
  returns nothing; work lands as direct pushes. So `pull_request` never
  fires today, and **anything designed around a PR's base commit has no
  event** — which is how Phase 7 is written ("on a pull request the base
  commit is already fetched"). On a push the base is
  `github.event.before`, which is all-zeros on branch creation and wrong
  after a force-push; and `actions/checkout` clones at `fetch-depth: 1`,
  so even `HEAD~1` is not in the checkout unless asked for. Decide which
  event Phase 7 runs on before writing it, not after.
- **A scheduled workflow only runs from the default branch.** A
  `schedule:` or a `workflow_dispatch:` in a file that is not on it is
  inert — no run, no error, nothing to notice. This bit here: `master`
  was the default while every commit went to `qt`, which made the whole
  nightly lane decoration. Fixed on 2026-09-02 by making `qt` the
  default (item 0.1), and worth re-checking the day the default changes
  again or a workflow is added on a side branch.
- **A scheduled workflow switches itself off.** *(No workflow here has a
  schedule since 2026-09-04; kept because the next person to add one
  will meet this.)* GitHub disables `schedule:`
  triggers in a repository with no activity for 60 days. Phase 6's whole
  argument is that a nightly job keeps a promise by existing — on a fork
  that goes quiet for two months it stops existing, silently, leaving a
  green history behind it. Keep `workflow_dispatch` on everything in that
  lane, and treat "the nightly has not run" as a condition somebody has to
  notice.
- **A third-party action pinned to a tag is a mutable dependency.** Pin
  the SHA and write the version beside it in a comment
  (`actions/checkout@fbc6f399… # v5.1.0`), and resolve it with
  `git ls-remote` rather than from memory. Phases 1–3 need no third-party
  action at all, which is worth keeping true for as long as possible.
- **But a pinned SHA also pins the Node runtime, and that is the half
  that rots.** The first real run warned that `actions/checkout@v4.2.2`
  and `actions/upload-artifact@v4.6.2` "target Node.js 20 but are being
  forced to run on Node.js 24". Nothing failed; the runner substituted a
  newer Node and said so. **The warning is the only mechanism that tells
  you** — a floating tag would have moved silently, and a pin will
  eventually stop being force-upgraded and break instead. So: read the
  warnings on a green run, and re-resolve the pins when one appears.
  Bumped to `checkout@v5.1.0` and `upload-artifact@v5.0.0` on 2026-09-02,
  both SHAs resolved with `git ls-remote`.
- **`tools/warning_audit.py` cannot be run with `--update` in CI**, and
  should not be. It flags *removed* warnings as well as new ones, on
  purpose, so a legitimate fix turns the job red until `tools/warnings.txt`
  is committed in the same change. That is correct behaviour and it is a
  workflow consequence: the baseline is part of the diff, not a thing CI
  maintains.
- **Never `git checkout` a file to undo a sabotage.** It reverts the
  file's entire share of the change. Twice in one session in this
  project. Reverse-patch the exact string.
- **The source is LF, and three categories are deliberately not.** This
  used to read "preserve CRLF", and it is the reverse now: work log item
  159 converted the tree, `.gitattributes` pins it with `* -text`, and
  `tools/line_endings_audit.py` fails on a carriage return in tracked
  source. What stays as it ships: binaries, files Windows or VMS tooling
  owns (`.rc`, `.sln`, `.def`, `makefile.com`), and the third-party
  `font/` distribution. **The data files** (`.as`, `.csv`) were a fourth
  until 2026-09-02, held back by a measurement that did not reproduce;
  they are LF now (work log item 173). A job that rewrites a tracked file
  must still not touch the three that remain.
- **`asan-sweep.sh` deletes `./astrolog`.** Isolate it.
- **`/swe` will never exist on a runner.** Anything that silently needs it
  tests nothing, quietly. That is the exact failure `-i nrvate.as` exists
  to prevent locally, and CI cannot use that lever.
- **`nrvate.as` must never be packaged.** It is the maintainer's personal
  settings file and it points `-Yi1` at a NAS mount.
- **Do not add caching until the builds are slow enough to need it.** They
  are not: ~60–70 s serial, measured. A stale cache reintroduces the
  stale-binary failure mode this project has already paid for twice.
- **A line-ending tool will eat the binaries unless told not to.** It has
  already happened once, to 28 files (3.1b). Any step that rewrites
  tracked files needs an explicit binary exclusion, and
  `.gitattributes`'s current list — `*.png`, `*.ico`, `*.bmp` — is not
  it.
- **Piped stdout is block-buffered, so a running binary looks silent.**
  `./astrolog-qt-test … | head` shows nothing until 4 KB accumulates or
  the process exits, which is indistinguishable from a hang and cost
  three probes here. In a workflow, redirect to a file and read it after,
  or accept that a step which greps a running program's output is
  measuring its buffer.
- **A settings file passed with `-i` can silently disable the whole run.**
  `-i astrolog.as` executes zero tests and exits 0 (2.0b). Any CI step
  that passes a settings file must be checked by *observing test output*,
  not by exit status.
- **A tool that ignores its argument cannot be tested by substituting a
  file.** Four of the audits hardcode `astrolog.rc`; handing them another
  path changes nothing and they pass, which reads exactly like a
  successful test of the other file. Before trusting any "I ran it
  against X" result, hand the tool a junk X and confirm it fails.
- **A measurement taken against a tree another session is editing is
  attributable to nothing.** This bit twice in one afternoon: once when a
  generator test measured a peer's uncommitted fix and was reported as
  validating the code before it, and once when the ephemeris under test
  was being rewritten mid-run. In CI this is free — a workflow checks out
  one ref — but any *local* measurement quoted in this document needs the
  commit it was taken at, or it is an anecdote.
- **A baseline binary must live in the repository root.** Astrolog builds
  its ephemeris search path from the executable's own directory, so a
  baseline anywhere else silently reads a different ephemeris and the
  differential reports a numerical regression that is not there (7.2b).
  `/base-astrolog` is reserved in `.gitignore` for exactly this.
- **The data files are not inert.** Converting `astrolog.as`, `atlas.as`,
  `timezone.as` and `nrvate.as` to LF moved six lines of
  `tools/switch-matrix.sh` output: `-0q` went from "Assuming first
  century C.E. …" to "Value 0 out of range from 1 to 12". Something in
  the atlas/timezone parsers reads a CR as content — undiagnosed, found
  by `astrolog-4f` on 2026-09-01, and the reason those four files ship
  unconverted. **A live bug**, and an argument for the differentials
  covering data files and not only code.
- **Two checks with different thresholds for the same property will
  disagree eventually.** A house-degeneracy guard tested `rGapMin > 0.0`
  while the oracle sweep used 0.001 degrees; Sunshine's narrowest house is
  a rounding error rather than an exact zero, so the guard passed a chart
  the oracle failed and a correct fix looked broken. Both use 0.001 now.
  Where CI runs two checks on one property, they need one threshold,
  defined once.
- **`0Ari00` is one signature with two causes.** A body reading
  `0Ari00'00"` means "no ephemeris" whether `ephem/` is missing from a
  package, `-Yi1` is unset in a config, or the search path was truncated
  for length. `CLAUDE.md` already records it for a missing `/swe`. Good
  news for CI: **one assertion covers all of them.**
- **The last line before a crash is the last *unbuffered* line, not the
  last thing that happened.** `PrintProgress` writes to stderr, which is
  unbuffered; the suite's own output is block-buffered once redirected to
  a file. So a crash log ends at the last stderr write, and the code that
  actually died may be far past it. This cost a wrong diagnosis today —
  every crashing run ended on "Writing wireframe to file." and the
  wireframe writer was innocent; the bug was in a transit search called
  later. **One gdb backtrace settled what hours of log-reading could
  not.** The same buffering also makes a running binary look hung when
  piped (`| head` shows nothing until 4 KB accumulates). In CI: redirect
  to a file and read it after, and never infer a crash site from output
  order.
- **Do not let "we were looking there anyway" promote a finding into a
  cause.** Two real defects surfaced while hunting this crash — an
  unreachable off-by-one in `WireNum` and a genuine bounded over-read in
  `WriteWire`, which reads up to five words past `pwWireCur` because its
  loop only checks `pw < gi.pwWireCur` with no room for a six-word
  record. Both are worth fixing. Neither was the crash, and treating
  either as the cause would have closed the hunt on a coincidence.
- **A retry is not a gate.** Where a check is flaky, the flakiness is the
  bug. Retrying it, averaging it, or marking it non-blocking all convert
  a defect that announces itself into one that does not — which is the
  same failure as a vacuous harness, arrived at from the other side.
- **Counts written into prose rot.** Three documents in this repo assert
  the suite's assertion count and all three are wrong (Q12). Anything CI
  prints should be read from the run, not restated in a document — and
  where a count *is* asserted, as in item 2.3, it belongs in code next to
  what produces it.

## Open questions

These are what "refining the document" means. Each blocks the item that
names it.

| # | Question | Blocks |
|---|---|---|
| ~~Q1~~ | ~~AppImage, tarball, or `.deb`?~~ **Answered 2026-09-02: native packages, both formats.** `.deb` for the last two Ubuntu LTS (22.04, 24.04) and `.rpm` for the last two Fedora releases. 6.0 MB and 5.9 MB, against ~70 MB for an AppImage's Qt closure. Built on each target and verified by installing into a clean container. | closed |
| ~~Q2~~ | ~~What version scheme does this fork own?~~ **Answered 2026-09-02: `8.00-qt.N`.** `szVersionFork` in `astrolog.h`; tag `v8.00-qt.N`; `.deb` `8.00+qt.N`; `.rpm` Version 8.00 Release `qt.N`. `szVersionCore` is left alone because the banner and `express.cpp`'s `atof()` read it. `tools/ci-assert-version.sh` fails a tag that disagrees. | closed |
| ~~Q3~~ | ~~How does Phase 1 smoke-test a GUI-only `.exe`?~~ **Answered: build `WCLI` too.** One-line guard fix at `astrolog.h:81`, drop `-mwindows`, and a console Windows binary runs non-interactively under Wine. Verified 2026-09-01. | closed |
| ~~Q4~~ | ~~Does anything read `astexo.csv` at runtime?~~ **Answered: yes, it ships.** `charts3.cpp:1792`, `-XUx`. | closed |
| ~~Q5~~ | ~~An optimized `-g` build for the fortify class?~~ **Answered: no separate build needed.** The ordinary `-O` builds already import 10–11 `*_chk` symbols, so Phase 7's matrices — which run against `./astrolog`, a normal build — already cover it. New item 3.4 asserts it stays that way. | closed |
| Q6 | Pass criterion for the Windows-vs-Qt text diff? **Largely answered by 6.4b**: pin `-z0 0`, filter the one path-syntax diagnostic, and the Windows/Linux text matrix diffs to **zero** — 4 lines of 6,936, all cosmetic. Still open only for the *GUI* route in 6.4, which also captures layout. | 6.4, 6.4b |
| ~~Q7~~ | ~~Are the differentials a gate or a report, and how is an intentional change signalled?~~ **Answered 2026-09-02: a gate, opted out of by a `Behaviour-change:` commit trailer.** Tested in both directions; the diffs upload as an artifact either way. | closed |
| Q8 | **Answered 2026-09-02 by the maintainer: both Qt5 and Qt6 are supported, from one build.** Not a migration and not a curiosity — `Makefile.qt` already asks pkg-config which Qt is present and builds against the better one, so a single `make qt` is right on either, and both measure `PASS: 3812 passed, 0 failed` from the same sources. Qt5 stays supported because the maintainer's own machine runs it; Qt6 is supported because Qt5 is past upstream's open-source support and the day a distribution drops it should be uneventful. **What this asks of CI is a second lane rather than a separate job**, and its shape is the CI author's call. Closed. | 8.2 |
| ~~Q9~~ | ~~Is `-ldl` harmless on macOS?~~ **Answered: keep it on Linux, drop it on macOS.** `dladdr()` is really called (`sweph.cpp:271`); the flag is a no-op on glibc ≥ 2.34 but still needed below it, and macOS puts `dladdr` in libSystem. | closed |
| ~~Q10~~ | ~~Does this fork distribute signed macOS binaries at all?~~ **Answered 2026-09-01: no. Superseded 2026-09-03: yes, ad-hoc signed.** Every release now ships `astrolog-<version>-macos.dmg`, built by `tools/package-macos.sh` on a `macos-14` runner and signed `codesign -s -`, which is mandatory on Apple Silicon and enough to run. It is **not notarized** — that needs a paid Apple Developer account — so a browser download is quarantined and the release notes say so on the page. First shipped in v8.00-qt.3. | closed, then reopened and answered differently |
| ~~Q11~~ | ~~Repair `Astrolog.vcxproj` or delete it?~~ **Answered 2026-09-02: repair it and build it in CI.** "Exactly one line behind" was wrong — it needed three fixes, two of which only a compiler could have found. | closed |
| ~~Q12~~ | ~~3526 or 2777?~~ **Answered: neither. Measured 3551 on 2026-09-01.** `2777` was written 2026-08-27 (`0a33328`), `3526` on 2026-08-31 (`2645464`); the suite has grown since. The lesson is not to pick a winner but to stop duplicating a number that rots by construction — the suite prints its own count, and three documents asserting it produced three wrong answers. | closed |
| ~~Q13~~ | ~~Minimal ephemeris only (A), check in the missing 8.0 MB (B), or fetch and cache in CI (C)?~~ **A′ done 2026-09-02**: `ephem/se52872.se1`, 93,806 bytes, so the bundled set passes with no gating and item 2.3 loses its special case. **B taken 2026-09-03**: the 20 files measured at 7.8 MB, and `ephem/` now resolves 39 of 39. The suite reports 3832/0 under `ASTROLOG_QT_EPHEM=minimal` and under `/swe` alike, where minimal used to run 21 assertions in that group against a local run's 41. Verified byte-identical to the source after copying, because this tree has corrupted `.se1` files before. **Corrected same day**: that measurement was wrong. `graphics-matrix.sh` defaults to `GRAPHICS_MATRIX_CFG="-i nrvate.as"`, which reads `/swe` — so the run that reported "no lines differ" could not see a change to `ephem/` at all. Under the config CI actually uses, `-Yi1 ephem`, adding the files moves **8 lines**. CI passed regardless, for a structural reason worth knowing: `ci-differential.sh` runs both binaries against the *current tree's data*, so a **data** change is invisible to it by construction. It compares binaries, not ephemerides. | closed |

## Work log

Same convention as `QT_GUI_PLAN.md`: one entry per item actually done,
recording what it turned out to be rather than what was planned. Every
falsification gets an entry saying what was broken and what the job said.

**2026-09-02 — PR 1 exists, and reviewing this document first was worth
more than PR 1.** The review came before the work, and it found five
claims that had gone stale in a single day — the day *after* the document
was written. Ranked by what they would have cost:

1. **Phase 8 was describing a world that ended overnight.** It said
   `qt6-base-dev` was "not installed, not needed" and filed "the port
   builds against Qt6" as an open prediction. `make qt6` had landed the
   previous evening and the suite passed 3561/0 against it. Phase 8 is
   rewritten and Q8 is reopened as a different question: not "build
   Qt6?" but "who keeps the Qt6 build alive?" — because it is a *fourth*
   configuration in the "compiled by nobody" category, on the day it was
   created.
2. **`graphics-matrix.sh` had been fixed and the document had not
   noticed**, still instructing a CI step to parse its last line for
   missing renders. Writing that parser would have been work spent on a
   bug that was already gone.
3. **The suite count.** This document asserted 3551 in three places while
   closing Q12 with the lesson *"stop duplicating a number that rots by
   construction"*. It was wrong by the next morning. The numbers are out
   now; CI reads the count from the run.
4. Six fast audits are eight; 155 tracked files are 164. Both re-measured
   rather than adjusted.

**And three findings that were not staleness but gaps**, since they
change what CI can be here: **this repository has no pull requests at
all** (`git log --merges qt` is empty), which is the event Phase 7's
differentials are designed around; **GitHub disables scheduled workflows
after 60 days of repository inactivity**, which is the mechanism Phase 6
relies on to keep a promise a human would forget; and **the suite runs
fewer assertions on a thinner ephemeris and only the pass count says so**
— 83 on `/swe`, 63 on `ephem/`, 53 pointed at nothing.

**What shipped.** `.github/workflows/ci.yml` with one job: the two
Windows builds and a freshness assertion on each. `Makefile.wcli` and a
one-line guard widening at `astrolog.h:81` revive `WCLI`, which nothing
had compiled since it was written. `tools/ci-assert-fresh.sh` is item
1.2, as a script rather than YAML, so it falsifies in a second instead of
a push. `ephem/se52872.se1` closes Q13 as A′.

**Both checks falsified, in a throwaway `git worktree`** rather than the
working tree — a second session was building in that tree, and a
deliberate syntax error there would have read as their bug. Details in
1.1, 1.2 and 1.3b. The sharpest of the three: breaking a line inside
`#ifdef WCLI` left `make -f Makefile.win` at **rc=0** and took
`make -f Makefile.wcli` to **rc=2**, which is the whole argument for
building the second one.

**What is not done and should not be read as done.** The workflow has
never run on GitHub. Item 0.1 — confirming Actions is enabled and what it
costs on this fork — is unchecked, and a green badge here is currently a
green badge in a text file.

**2026-09-01 — item 2.0b measured, and it found a live data-corruption
incident.** Testing this document's own claim that CI could run against
the bundled ephemeris turned up four things, in order of how they were
found:

1. `ephem/sepl_18.se1` was **damaged in the working tree** — the program
   said so out loud. Diagnosis: the tree-wide CRLF→LF conversion (work
   log item 159) had stripped carriage returns from **28 binary files**,
   bytes lost equal to CR count in all 28. Not committed; HEAD was
   intact; restored the same day and verified byte-identical.
2. **The 3551-assertion suite passed throughout**, because
   `run-qt-tests.sh` defaults to `-i nrvate.as` and therefore reads
   `/swe`, never opening the corrupted files. The only thing that caught
   it was running against `ephem/` — which is what this document asks CI
   to do. Item 2.0's argument, demonstrated rather than asserted.
3. **This document's own instruction was vacuous.** `-i astrolog.as` runs
   zero tests and exits 0. A CI job written from the earlier draft would
   have been green while executing nothing. Corrected to `-Yi1 ephem`;
   recorded because ground rule 1 exists for exactly this and its author
   still walked into it.
4. **The suite fails on the bundled set**, 2 assertions, not the graceful
   degradation the document predicted — asteroid 52872, a 92 KB file
   away from passing.

The guards remain open; see 3.1b.


**2026-09-03 — the release refused to publish, counting seven of the
nine artifacts it was printing on screen.** `v8.00-qt.3` built every
package, verified the NSIS installer three ways, and then failed
`Publish`:

```
WRONG ARTIFACT COUNT: 7, expected exactly 9.
== what is there:
  ...nine files, listed in full...
```

The counter in `tools/ci-verify-release-dist.sh` matched `*.deb -o *.rpm
-o *.zip`, written when those were the only kinds. `.dmg` and `.exe`
joined the release later.

The number is the boring half. **The shape is the finding: the script
held the same list twice, in two places that must stay exact
complements** — the artifact count, and the stray-file check ("anything
that is *not* an artifact would be published too"). Whichever copy you
update, the other is now wrong, and the two failure modes point opposite
directions. Updating only the count gets what happened. Updating only the
stray list would have failed the release with *"STRAY FILES ... these
would be published too"* naming the two artifacts it exists to publish.

One list now, `kinds`, with both `find` expressions generated from it. A
new artifact type is one word in one place, and the two checks cannot
disagree about what an artifact is. Falsified against the real filenames
three ways: nine passes, eight fails with the right count, and an
unrecognized file is reported as a stray rather than dropped from
SHA256SUMS.

**And the same day, `PATH_SEPARATOR` turned out to be a character class.**
Swiss defines it as `";:"` on Unix — the comment in `sweodef.h` reads
*"semicolon or colon may be used"* — and passes it to `strchr()` as a
set. `CDirJoinEphemQ()` was joining with the whole two-byte string, which
works (`swi_cutstr` skips runs of delimiters, so no empty entry appears)
but spends two bytes of a 242-byte budget on every join and prints
diagnostics like `.;:/usr/share/ephem;:/usr/share/font`. Now
`.;./ephem;./font/`.

Two things came out of reading that code that the byte count did not:

1. **`cDirEphemMax` being 20 is not slack, it is Swiss's own ceiling.**
   `swe_set_ephe_path()` parses with `swi_cutstr(s, PATH_SEPARATOR, cpos,
   20)`, which stops cutting once it has 20 pieces. A 21st directory is
   not ignored — it is *glued to the 20th*, and the pair becomes one
   nonexistent path. The constant now says so, because the next person to
   raise it would have had no way to know.
2. **The obvious name for the new constant was already taken, by a
   different separator.** `chDirSep` is `'/'` on Unix and `'\\'` on
   Windows — directory glue, used twenty lines further down to walk back
   through the executable's own path. A `#define chDirSep
   (PATH_SEPARATOR[0])` above those uses would have compiled, warned
   about a macro redefinition, and silently made the exe-path parser
   search for `';'`. Renamed to `chEphemSep` before building. Filed here
   because it is the third time in this project that a plausible name has
   collided with an existing one in the same translation unit, and the
   only thing standing between it and a shipped bug was `-Wall`.

**2026-09-03 — the installer's Properties tab was blank.** `astrolog.nsi`
took `VERSION` on the command line and used it for the window title, the
`Software\Astrolog` registry key and the Add/Remove Programs row, but
declared no `VIProductVersion`, so the PE carried no version resource at
all. Right-clicking `astrolog-setup.exe`, choosing Properties and opening
Details showed nothing: no product name, no version, no copyright. That
is what corporate software inventories read, and it is one of the things
that makes an unsigned installer look worse than it is.

The reason it was missing is worth writing down, because it is a trap
rather than an oversight. **`VIProductVersion` is not the version
string.** It must be exactly four dot-separated numbers, and `makensis`
rejects `8.00-qt.3` outright rather than coercing it. So there are two
defines now, not one: `VERSION` for everything a human reads, and
`VERSIONQUAD` — `8.0.0.3`, derived in the packaging script from the same
`szVersionFork` everything else uses, with the leading zero stripped so
Explorer does not show `8.00.0.3`.

Because `VERSION` arrives on the command line, nothing structurally stops
a caller passing a stale one, and a wrong version in Add/Remove Programs
is invisible until a user has two Astrologs installed side by side. So
`tools/ci-verify-windows-installer.sh` now reads the resource back out of
the built `.exe` and requires it to equal what `astrolog.h` says. It uses
`strings -el` rather than `exiftool`: the resource is UTF-16LE, binutils
is on every runner already, and the keys and values alternate, so
`grep -A1 -x ProductVersion` is the whole extraction.

Falsified by building an installer with `-DVERSION=9.99-qt.99` on purpose:
the check names both versions and exits 1, and exits 0 on the real one.

**2026-09-03 — the external oracle never ran, and the commit adding it
said it did.** `b56f511` is titled "The external oracle runs in CI after
all: a clone and a build is 25s". It does not run. It has failed on every
nightly since it was added, on its first execution and every one after,
with:

```
no astrolog at ./astrolog
```

The clone and the build were fine — the log shows `ar: creating libswe.a`
right before the guard fires. What is missing is Astrolog itself. The
step was put in the sanitizer job because that is the slow lane, and
**nothing in that job ever builds `./astrolog`**: both sweeps compile
their own instrumented binaries and run `make clean` on either side of
doing so. `asan-sweep.sh`'s header says so in as many words, and
`CLAUDE.md` repeats it — *"it deletes `./astrolog` while it runs"*. It was
written down, in two places, and still walked into.

The fix is one `make -j4` in the step, deliberately unsanitized: the
question is whether a *number* is right, and an instrumented binary
answers it identically but slower, on paths the sweeps have just covered.

**The transferable part is not the fix.** A step was added, a commit
message asserted it worked, and the assertion was never checked against a
run. Every other check in this document was falsified before being
believed; this one was not, because it had passed *by hand* on the
developer's machine — where `./astrolog` happens to exist, since
everything else builds it. Passing locally is not evidence a CI step
passes, and the environments differ in exactly the way that matters here:
what else has already run in the same working directory.

Reproduced locally before fixing, by moving `./astrolog` aside and
re-running: the same message, and the same 50-comparison pass once it is
put back.

**2026-09-03 — v8.00-qt.3 published, and the whole chain ran for the first
time.** Nine artifacts, each verified before publication, plus a
SHA256SUMS covering all of them; then `repo.yml` chained off it and
rebuilt the apt and dnf repositories, including its own "install from it,
on every distribution it serves". The nightly on the same commit is clean
in all six jobs, and the external Swiss oracle inside it reported *50
comparisons, worst deviation 0.0005 degrees* — its first successful run.

The tag was **moved** rather than bumped. `v8.00-qt.3` had been pushed
days earlier and had never produced a release, so nothing referenced it;
moving a pointer to a version that has not shipped is what a tag is for.
`ci-assert-green.sh` refused twice before it was allowed to, both times
correctly: each new push superseded the previous CI run via the
concurrency group, and a *cancelled* run is not a green one.

**And one gap remained after all of it.** `ci-verify-repo.sh` checks the
repository `repo.yml` just built, in the job that built it. What nothing
checked was the half that happens afterwards — the Pages deploy. A deploy
can succeed and serve the previous commit with every check upstream of it
green. `tools/ci-verify-live-repo.sh` fetches the indexes over HTTPS the
way apt and dnf will and requires the version to be in them, for all six
distributions.

Writing it produced a good demonstration of why a harness gets sabotaged
before it is believed. Its first run reported **four of six distributions
broken** — apt fine, every rpm repository missing the version. Both
failures were in the check:

1. The `primary.xml` href was extracted as the whole
   `location href="..."/` string rather than the attribute value, so
   `curl` was handed a URL containing a quote and failed with "bad/illegal
   format", which reads like a missing file.
2. **RPM does not store a version string at all.** `primary.xml` splits it
   into `ver="8.00" rel="qt.3.fc42"`, so `8.00-qt.3` appears nowhere in
   the metadata. Grepping for it can only ever fail.

That is three spellings of one version across the release — `8.00+qt.3`
for dpkg, `8.00-qt.3` in an rpm filename, and a split pair in rpm
metadata — and none of them is negotiable. The repository had been
correct the entire time.

**2026-09-03 — `-XE 1 20` had been an inert line in the graphics matrix
for its whole life.** Chasing whether the new ephemeris files changed any
render turned up something else: `-XE 1 20` renders **byte-identically to
no `-XE` at all**, in every one of the twenty chart modes tested.

The reason is not that `-XE` is broken. The asteroid loop stops at the
first body it cannot compute, and asteroid 9 has no ephemeris file — so
the range drew nothing, not even asteroids 1 through 8, which resolve
perfectly. Measured under `-XG`: `-XE 1 8` gives `a09566b8`, `-XE 1 9` and
`-XE 1 20` both give `783eb0a5`, and `783eb0a5` is bare `-XG`.

Replaced with `-XE 1 8`, the largest low range that fully resolves, and
`-XE 433 433` — Eros, a single body from the newly bundled set, written as
a single body rather than a range because a range that silently truncates
is precisely what went wrong. 224 renders became 227, and removing five
ephemeris files now moves **16** lines of the matrix where it moved 8.

Two lessons, and the second is the more useful:

1. A harness entry can be inert for years while looking like coverage.
   This one survived every review because `-XE 1 20` is a *plausible*
   range, and nothing checked that it drew anything.
2. **The measurement that found it was itself wrong first.** The "no
   lines differ" result above came from a matrix run reading `/swe`
   rather than `ephem/`, because that is the script's default. Two wrong
   measurements in a row, on the same question, in the same hour.

**2026-09-03 — measuring what the differentials execute, instead of
guessing.** After `-XE 1 20` turned out to have been inert since the day it
was written, the obvious question was how many others are. Anecdote does
not scale; `gcov` does. `tools/coverage-report.sh` builds an instrumented
console binary, runs all four matrices against it and reports per-file
line coverage.

The four matrices together, over the fork's own sources:

| coverage | lines | file |
|---|---|---|
| **0.00%** | 563 | `placalc.cpp` |
| **0.00%** | 135 | `placalc2.cpp` |
| 5.02% | 3166 | `swecl.cpp` |
| 19.66% | 773 | `xscreen.cpp` |
| 20.49% | 1142 | `express.cpp` |
| 29.25% | 294 | `matrix.cpp` |
| 43.18% | 1957 | `io.cpp` |
| 46.62% | 1613 | `xdevice.cpp` |
| 47.43% | 2994 | `xcharts1.cpp` |

**`placalc.cpp` at 0% is not dead code.** Removing both files from
`Makefile.srcs` fails to link: `calc.cpp` calls `julday()`, `revjul()` and
`FPlacalcPlanet()`. They are compiled, linked and referenced, and no
matrix executes a line of them.

The reason took some digging and is worth writing down. `-bp` selects the
Placalc backend and is refused with *"The switch -bp is not allowed now."*
because the shipped `astrolog.as` contains `=0b`, which sets
`us.fNoPlacalc` — and `switch.cpp:3653` is `case 'b': us.fNoPlacalc =
fTrue;`, a **one-way latch with no switch that clears it**. Every matrix
run reads that settings file, so the Placalc and Matrix backends are
unreachable in all of them.

Run without `astrolog.as` present, `-bp` is accepted, and then the trap
documented at `switch.cpp:2168` bites: the backend suffixes of `-b` also
*toggle* `fEphemFiles`, so a plain `-bp` turns the files off and every
body reads `0Ari00'00"`. `-bp -b` computes, but agrees with Swiss at
display precision, so whether it reached Placalc is still unproven.

Left open rather than guessed at: whether a matrix should cover Placalc,
and how to invoke it so that it demonstrably does. What is settled is that
698 lines of a linked, called backend currently have no differential
coverage at all, and that the number came from measurement.

**2026-09-03, later — the same measurement against the other half of the
apparatus, and a definitive answer.** The matrices are not the only net;
the 3832-assertion Qt suite is the other, and it reaches different code.
Measured, they are genuinely complementary rather than overlapping:

| file | matrices | Qt suite |
|---|---|---|
| `xdevice.cpp` | 46.6% | 14.6% |
| `express.cpp` | 20.5% | 38.6% |
| `swecl.cpp` | 5.0% | 22.3% |
| `switch.cpp` | 77.7% | 39.3% |

So the interesting number is not either column but what **both** miss.
Over the 29 files measured by both:

```
   563 lines  placalc.cpp
   135 lines  placalc2.cpp
   TOTAL: 698 lines nothing in this project executes
```

That is the whole list. Every other file in the build is entered by at
least one net. 698 lines are compiled, linked, called from `calc.cpp`, and
never run by anything — for the reason recorded above: `-0b` in the
shipped `astrolog.as` is a one-way latch that makes the Placalc backend
unselectable, and every net reads that file.

The next blind spots after those, by best-of-both:

| best of both | lines | file |
|---|---|---|
| 9.66% | 383 | `swejpl.cpp` |
| 14.56% | 206 | `swedate.cpp` |
| 22.33% | 3166 | `swecl.cpp` |
| 38.64% | 1142 | `express.cpp` |
| 39.88% | 2119 | `swephlib.cpp` |
| 41.86% | 1739 | `swehouse.cpp` |

`swejpl.cpp` and `swecl.cpp` are the JPL-file and eclipse paths, both
needing data this repository does not ship, so low coverage there is
expected rather than alarming. `swehouse.cpp` at 41.9% is the surprise
worth a look: the suite asserts all 40 house systems partition the circle,
and that still leaves more than half the file unentered.

**2026-09-03 — the Windows package half, re-tested and partly closed.**
Item 4.4 has said the Windows package cannot be verified the way the Linux
one is: `astrolog.exe` has no console entry point, so "install it, run it
from `/`, assert Chiron" does not transfer, and WCLI resolves its data
through a different branch of `SwissEnsurePath()` so it does not
substitute. **Both halves re-tested and both still hold.** `astrolog.exe`
blocks forever under `wine ... -os file`, with a display and without: it
is a `WinMain` program and always opens a window, so it cannot be driven
to write a chart and exit.

What can be asserted is weaker, and nothing was asserting it at all:
**the packaged binary starts.** `tools/ci-verify-windows-starts.sh` runs
it under Xvfb and requires a window whose title begins "Astrolog" —
measured, `Astrolog 8.00`. That covers a statically-linked `.exe` missing
a DLL, a corrupt build, and a binary that crashes on startup, none of
which a file listing or a checksum can see. Falsified with a truncated
`astrolog.exe`: no window, exit 1.

It kills by the PID it started. `pkill -f astrolog` matches the
maintainer's own running copy and has killed it before (`QT_GUI_PLAN.md`
work log item 169).

Item 4.4 stays open for the strong form — "the installed Windows package
computes a correct chart" — which needs the GUI driven with `xdotool`, and
that is `tools/win-tests.sh`'s territory rather than a package check's.

**2026-09-03 — GCC's static analyzer, run once over the fork's own
sources, and what it did not find.** After two real bugs came out of the
first UBSan run over the Qt suite, the same question — what surface has
never met what tool — pointed at `-fanalyzer`, which nothing here had ever
used. Nineteen files, every one of the fork's own `.cpp` (the vendored
Swiss set excluded), several minutes each.

**One diagnostic, and it is a false positive.** A null dereference of
`pchCur` at `express.cpp:2138`, traced from `ShowParseExpression` through
`GetParameter`. Every step on that path is in fact guarded:

* `express.cpp:2062` wraps the macro call in
  `FSzSet(xi.rgszExpMacro[n1])`, and `FSzSet` is
  `((sz) != NULL && *(sz) != chNull)` — a null check the analyzer does not
  model, because it is a macro rather than a function it can reason about.
* Both recursive call sites check the result: `if (pchCur == NULL) return
  NULL;` at 2256, and `while (pch != NULL && ...)` at 2297.

So the answer is "clean", and the cost of establishing that was one
afternoon of machine time and no code change. Recorded here so the next
person does not re-run it expecting a haul.

Two things worth keeping from it anyway. **`-fanalyzer` needs `-c`, not
`-fsyntax-only`** — the analysis runs during code generation, so a syntax
check silently produces zero diagnostics and looks like a clean result.
That was measured on deliberately broken code: a use-after-free and a null
dereference that `-fsyntax-only` reported not at all and `-c` reported
both. And it **does** work on C++ in GCC 11, contrary to the
"C only" reputation the documentation of earlier versions earned it.

**2026-09-03 — two build artifacts reached commits, and only one was
noticed.** `git add -A` swept `astrolog-qt-ubsan` (26.6 MB) into
`5aa4572` and `astrolog-ubsan` (12.7 MB) into `273a0ac`. 39.3 MB into a
repository whose `.git` was 56 MB.

The first self-corrected: `tools/ubsan-sweep.sh` deletes its own Qt binary
at the end, so the next `git add -A` recorded the deletion. It was tracked
for exactly one commit and nothing ever said so. The second stayed, and
CI failed on it — `line_endings_audit.py`, the only check that could see
it, because a binary is full of carriage returns.

Three things worth keeping:

1. **The catch was incidental.** Nothing here checks for tracked build
   artifacts; a line-ending audit noticed because ELF happens to contain
   `\r`. Its message led with "carriage returns in 1 of 156 tracked TEXT
   files" and advised stripping them, which on an ELF destroys it. Fixed:
   it recognises ELF/PE/Mach-O magic and now leads with what the file is.
2. **`.gitignore` covered `astrolog-qt-asan` and `obj-qt-asan/` and had no
   entry for any UBSan artifact.** The sanitizer added on the same day
   inherited none of the older one's hygiene.
3. **The count in the first fix's commit message was wrong.** It said
   "this was the only one", from a sweep of the current index — true, and
   incomplete, because the other had already been removed by a script
   rather than by anyone noticing. A question asked of the index cannot
   see what history holds.

Both blobs stay in history. Removing them means rewriting published
history, which is the maintainer's call rather than a CI tick's.

**2026-09-03 — the Qt6 warning ledger cannot be checked on the runner that
checks the others, and installing Qt6 does not fix it.** `warning_audit.py`
has always printed `qt6: skipped, no Qt6` in the nightly, so
`tools/warnings-qt6.txt` — the ledger of what Qt6 warns about and Qt5 does
not — is verified by nothing. It is empty, and an empty baseline nothing
checks is indistinguishable from a clean one.

Adding `qt6-base-dev` to that job looked like the fix. It is not, and the
measurement says why: **the `ubuntu:22.04` image's `qt6-base-dev` ships
zero pkg-config files**, checked in the image itself, so
`pkg-config --exists Qt6Widgets` cannot succeed there whatever
`PKG_CONFIG_PATH` holds. The message changed from naming a path to naming
an empty one; the skip did not move.

Nor can the job move to a newer image. It is pinned to `ubuntu-22.04` for
its **compiler**: `tools/warnings.txt` is a ledger of what g++ 11 says,
and `ci-assert-toolchain.sh` fails when the major version differs, because
g++ 13 objects to things g++ 11 does not. `ubuntu-latest` has Qt6 with
`.pc` files *and* g++ 13. The two requirements cannot be met on one runner.

**Open.** Closing it means a second warnings job on a newer image with its
own baseline for that compiler — which is a real piece of work, since the
first run of a new baseline is a wall of warnings that has to be read
rather than accepted. Recorded rather than papered over, because a
`qt6-base-dev` in that install line would have looked like the problem was
solved.

**Closed the same day, and the paragraph above was wrong about the cost
(`c989607`).** No second baseline is needed, because the Qt6 leg is a
*difference*: `audit_qt6()` measures each Qt6 build against its Qt5 twin
built in the same run by the same compiler, so whatever g++ 13 says about
the shared core appears on both sides and subtracts out, leaving exactly
the Qt6-specific set `warnings-qt6.txt` holds. `--qt6-only` builds the
two Qt5 twins for their counts, skips the g++ 11 baseline, gates on the
Qt6 ledger alone, and treats a missing Qt6 as a failure rather than a
skip. The `warnings-qt6` job runs it on `ubuntu-latest`; the `warnings`
job stays on 22.04 for its compiler; `ci-assert-slow-lane.sh` tolerates
the 22.04 skip only while `warnings-qt6` is green.

**2026-09-04 — the nightly is gone, Fedora is asked for, and Windows
ships the Qt build.** Three maintainer decisions in one session, all
recorded in "Where this stands" above with their reasons; this entry is
what the work turned out to be.

*What was wrong before anyone touched it.* The Fedora rows said 42 and
43 in four files. Fedora 42 had been end-of-life since 2026-05-27 and 44
current since 2026-04-28 — measured against Bodhi and endoflife.date,
which agreed. The "Where this stands" section was dated two days earlier
and wrong on three counts: it listed the nightly with four jobs when it
had eight, said the macOS suite hangs when it passed in six minutes, and
said the release-to-repository trigger had never fired when it had
fired four times that day. CLAUDE.md said seven nightly jobs. A project
with a written rule about counts in prose rotting had let them rot.

*The nightly.* Removing the schedule cost nothing that was being used.
What it held was moved, not deleted: every job that produced a check is
in `slow-lane.yml` and gates the release; the macOS job stopped being
`continue-on-error` and now uploads the `.dmg` the release publishes, so
`release.yml` lost its own macOS job, which had built and packaged
without ever running the suite. The Qt-on-Windows job left for
`windows-qt.yml`. "Behaviour vs yesterday" was dropped. The one thing
that had to change its nature was `tools/ci-assert-nightly.sh`, now
`ci-assert-slow-lane.sh`: its staleness check assumed a cadence, and a
lane that runs on releases has none, so that check is off unless
`MAX_AGE_H` is set.

*Fedora.* `tools/distros.py` was tested three ways before it was wired
in: Bodhi answering (43, 44), Bodhi unreachable and endoflife.date
answering the same, and both unreachable — which exits 1 with a message
naming `FEDORA_RELEASES`, because a matrix that guesses is worse than a
red job. The Fedora 44 package was then built the way CI builds it,
inside a `fedora:44` container on the maintainer's machine, against
`qt6-qtbase-devel`: `rpmbuild` produced it, it installed, ran from `/`,
and computed Chiron at 16Can03, with `libQt6Core.so.6(Qt_6.11)` in its
computed Requires.

*Windows.* Five new tools and one changed one. `tools/msvc-build-qt.cmd`
is the two inline `cl.exe` blocks made one script with one flag list;
the only difference between the two binaries it builds is `/DQTTEST`
and the subsystem. `tools/astrolog-qt.rc` gives the executable an icon
and a version resource, and was compiled on Linux with mingw's
`windres` — which found that windres passes `-D` values through a
shell, so the string needs `'\"8.00-qt.5\"'` there where `rc.exe` takes
`\"8.00-qt.5\"`. `tools/version.py` exists because that `.cmd` cannot
run `ci-assert-version.sh`, and its first draft read `szVersionFork` as
the whole version and printed `5`. `tools/package-windows-qt.py` stages
the tree, and its dry run on Linux with a stub `windeployqt` and a stub
redist directory fed the real `qt_windows_dist_audit.py`,
`ci-verify-package.sh`, `ci-verify-zip.sh`, `package-windows-installer.sh`
and `ci-verify-windows-installer.sh` under Wine, all of which passed on
the stubbed tree with the Win32 binary standing in — and refused when a
plugin or the runtime was removed. `tools/win-qt-starts.ps1` is the
check that needs Windows, and the only new logic that could not be run
here.

*A hazard worth its own line.* The release's Publish job downloads
artifacts by the pattern `astrolog-*` and publishes everything it gets.
The called workflows upload more than what ships — a staged tree, a test
build, screenshots, logs, a coverage report — and every one of those is
named without the prefix so the pattern leaves it behind. A new upload
in either called workflow that starts with `astrolog-` would be
published as a release asset, and `ci-verify-release-dist.sh` would
fail the release on the stray. That is the intended failure, and the
prefix rule is written at the top of `windows-qt.yml`.

**2026-09-04, later — making the slow lane a gate found the flake in
it.** The first dispatched run failed "Windows parity" at "The packaged
Windows program starts": exit status 255, no window, nothing on stdout
or stderr, metacity mapped. That step had failed in three of the
previous five nightly runs and been fixed three times — a window
manager, a wall-clock deadline, better diagnostics — each fix real and
none the cause. As a nightly that was a red run somebody read or did
not; as a release gate it is a release that fails at random, so it had
to be found.

The status was the clue. Astrolog's own exits go through `Terminate()`,
which calls `exit(NAbs(tc))` and cannot produce 255. `WinMain` returns
-1 in exactly two places — `RegisterClass` failing and `CreateWindow`
failing — and both call `PrintError()` first, which on Windows is a
`MessageBox` that needs the same display driver the window did. So:
a process that could not create a window and could not say so.

The steps before it in that job boot the Wine prefix and run the
console checks with **no DISPLAY**. `wineserver` lingers three seconds
after its last client exits, and `explorer.exe` with it, having loaded
no display driver. If the startup check reaches `wine` inside that
window — Xvfb and metacity coming up in under three seconds on a fast
runner — the program joins that session, `CreateWindow` returns NULL,
and the shell sees 255. `WINEDEBUG=-all`, set so Wine's fixme noise
stayed out of the log, also silenced the one line that would have
named it: `err:winediag:nodrv_CreateWindow`. Slower runners passed.

Reproduced on the maintainer's machine, same Wine 6.0.3 as the runner:
boot a fresh prefix with `DISPLAY` unset, run a console program, run the
check at once — 255, no window, four of four attempts the other way
round with a display had passed. Put `wineserver -w` between the two
and it opens "Astrolog 8.00". The fix in
`tools/ci-verify-windows-starts.sh` is `wineserver -k` then
`wineserver -w` before launching, so the program always starts a
session of its own with the display it is given, and `WINEDEBUG=err+all`
so the next failure of any kind carries Wine's account of it. The
diagnostic text also stops asserting "a crash on startup or a corrupt
binary" as the only reading of a silent exit, since for three runs it
was neither.

`tools/windrive.sh` had killed the server before every run "because it
outlives the app, and a leftover one makes the next run flaky" since it
was written. The knowledge was in the tree, one file over.

**2026-09-04, v8.00-qt.6 — the first release through the new shape, and
what it measured.** Tagged after `ci-assert-green.sh` on the full SHA
(the first attempt passed a short SHA, which `gh run list --commit`
does not match, and the script correctly refused rather than tagging
a commit CI "had not seen"). Every job the release now depends on ran
and passed: the slow lane's seven, the Windows build and installer, two
`.deb`s, four `.rpm`s including Fedora 43 and 44 chosen by the matrix
job, and Publish. `ci-verify-published-release.sh` downloaded all nine
artifacts and every digest matched; the notes carried the computed
"Ubuntu 22.04/24.04" and "Fedora 43/44"; `ci-assert-slow-lane.sh`,
pointed at the release run, found every step of all eighteen jobs
succeeded with none silently skipped.

| | |
|---|---|
| release wall time | about 18 minutes, created 06:15:47Z |
| long pole | AddressSanitizer sweep, 996 s |
| next | macOS build + suite 325 s, coverage 324 s, Windows build 297 s |
| Windows artifacts | `.zip` 26.2 MB, `setup.exe` 21.2 MB — the Qt build with its runtime, against 14.1 and 12.6 for the Win32 build it replaced |

Two per-push costs were taken off the Windows job the same morning,
each measured before and after on the runner:

- **`/MP`.** `cl.exe` compiled the 34 sources one at a time: 109 s for
  the program and 106 s for the test build, 215 of the job's 311
  seconds. With one worker per core: 61 s and 55 s.
- **The Qt cache.** A hit restores in 8–10 s and asks no mirror
  anything; the miss on the first run installed and saved it. Pinned to
  `actions/cache` v5.1.0 rather than v4.3.0, because v4 targets Node 20
  and every run carried the runner's warning that it was being forced
  onto Node 24.

And the last hand-written distribution list is gone: the `.deb` rows in
both workflows come from `tools/distros.py` through the same job as the
rpm rows, with the container named in the row instead of by an inline
expression. They stay static in the script because they are runner
images, and `actions/runner-images` lists no `ubuntu-26.04` yet.

`tools/ci-assert-slow-lane.sh` takes a run id now. Without one it looks
at slow-lane.yml runs, which since today are only by-hand dispatches;
the run worth asking about is a release run, where the lane's jobs
actually execute.

**2026-09-04, later still — old releases are pruned, and Ubuntu is asked
for.** Two maintainer decisions after the release, both from the second
look's list of what was still off.

*Pruning.* "We absolutely want to prune old releases." The site had
been serving every package ever published, Fedora 42's included, and
`ci-verify-repo.sh` was describing that suite as served-but-not-verified
— honest, and not a state anyone wanted. Two scripts, on the two axes:

- `tools/prune-releases.sh [keep] [--dry-run]` retires every release but
  the newest N. Two by default, the smallest number that leaves the
  repository's upgrade check something to upgrade from. Run by
  `release.yml`'s Publish job right after `gh release create`, so cutting
  a release is what retires the ones before it. The git tags stay; the
  assets go, which is why `--dry-run` exists and was run first. Run by
  hand the same afternoon: v8.00-qt.1 through qt.4 deleted, six tags
  still on the remote.
- `tools/collect-release-packages.sh` replaces the inline download loop
  in `repo.yml` and keeps only packages for distributions
  `tools/distros.py` names today, printing each one it drops. Run by
  hand against the two remaining releases: eleven kept, one dropped —
  qt.5's `fc42`.

Between them the site serves the newest two releases for the
distributions currently built, which is exactly the set the rebuild
installs, upgrades and verifies in a clean container.

*Ubuntu 26.04.* The answer to "what can we do" was that the `.deb` jobs
never needed to run ON a runner image; the rpm jobs have always run
INSIDE `fedora:<version>` containers and a Docker image exists the day
a release does. `tools/build-in-container.sh <image> <qt-package>
deb|rpm` does that for either family, from the host, so the strong
verification stays: `ci-verify-linux-package.sh` still installs the
result into a fresh container afterwards. Measured before it was wired
in, the way the Fedora 44 row was:

| image | Qt | build + package | verified in a clean image |
|---|---|---|---|
| `ubuntu:26.04` (resolute) | qt6-base-dev, Qt 6.10.2 | 65 s | Chiron 16Can03; Depends computed as `libqt6core6t64 (>= 6.10.2)` … |
| `ubuntu:22.04` (jammy) | qtbase5-dev, Qt 5.15 | 52 s | Chiron 16Can03; Depends computed as `libqt5core5a (>= 5.15.1)` … |

With the runner image out of the picture, Ubuntu is asked for like
Fedora: Launchpad's series API, every LTS within Canonical's five years
of standard support — 22.04, 24.04, 26.04 today — with endoflife.date
as the fallback and `UBUNTU_RELEASES` as the override, and a loud exit
when neither answers. **Not Launchpad's own `supported` flag**, which is
true for 20.04 as well because Ubuntu Pro extends it; that is support a
user pays for. Qt5 up to 24.04 (22.04's `qt6-base-dev` ships no
pkg-config files, measured; 24.04 keeps the Qt5 lane the maintainer's
machine runs), Qt6 from 26.04.

Seven Linux package jobs per push now, not six, and the last hand-written
distribution row is EL9/EL10.

*What the runner and the site said afterwards.* CI on the commit:
seventeen jobs, green, about three and a half minutes wall; the three
Ubuntu rows built inside their containers in 94 s (22.04), 144 s
(24.04) and 107 s (26.04), the Windows job at 208 s with `/MP` and the
cache against 311 s the day before. The dispatched repository rebuild
printed `dropping astrolog-8.00-qt.5.fc42.x86_64.rpm: fc42 is no
longer built`, verified every remaining suite (install, upgrade qt.5 to
qt.6, remove clean; fc44 single-version), and after the Pages deploy
`rpm/fc42/` answers 404, `rpm/fc44/` 200, and each apt suite lists
exactly `8.00+qt.5` and `8.00+qt.6`. The `resolute` package reaches the
site with the next tag; until then `ci-verify-live-repo.sh` wants the
release's own list (`DISTS=...`, as the slow lane's job passes it),
since the default list now names a distribution nothing has been
published for.

**2026-09-04, evening — the second look's four smells, taken in order.**
The maintainer took the list as given: "I'll take the fixes in your
order." Four commits, each measured:

1. **The MSVC project job out of the fast lane** (`a376ae0`). It
   compiled the Win32 program under MSVC on a Windows runner per push,
   and that program is the oracle now, not the product. It gates
   releases from the slow lane instead; dispatched there the same
   evening and passed in 56 s.
2. **The rpm rows verify to the deb standard** (`e1d38bc`). They ran
   inside `container: fedora:44` and verified with `local`, where a
   wrong `Requires` line is invisible. They build through
   `tools/build-in-container.sh` from the host now and are installed
   into a fresh image. Proven here first: fedora:44 in 98 s, rockylinux:9
   in 72 s, each computing Chiron in a clean image of itself. On the
   runner the rows cost more than before -- 154 to 174 s for Fedora
   against about 90 -- because a fresh container's dnf replaces a
   pre-warmed job container and a second container is started to verify
   in. That is the price of the stronger check and it is paid on every
   push: job time rose from about 1,530 to about 1,865 seconds, wall
   from 339 to 368.
3. **Two dead guards removed** (`d42ff78`): the "served, but no longer
   in the build matrix" branch that the collect filter had made
   unreachable, and the published-release verifier's skip list for
   three releases that no longer exist. A suite served and not built
   now fails as the bug it would be.
4. **The matrix is a committed snapshot** (`e0e2930`). `tools/distros.json`
   is what every read uses, so a build is a function of its commit and
   no network is touched except by `distros.py check`, which the matrix
   job runs first and which fails the first push after a release ships
   or ages out, naming the row and `--update`. Falsified three ways
   before wiring: no snapshot, an edited snapshot, a forced live answer.

CI on the four: sixteen jobs, green, 368 s wall.

**2026-09-04, night — the third look's four smells, and what the push
lane costs now.** Taken in the order recommended, one commit each:

1. **The drift check is its own job** (`3e5bcaa`). Inside the matrix
   job it would have turned the first push after a Fedora release red
   and skipped every package job for it -- the "punishes whoever pushed
   last" shape, reintroduced by the author the same afternoon he cited
   it. Now the packages build from the snapshot regardless, the run
   shows a red job named "The distribution snapshot is current", and
   `release.yml` keeps the hard gate.
2. **A push builds one row per family** (`19d5c2b`): the newest Ubuntu
   LTS and the newest Fedora, through `distros.py deb --push` and
   `rpm --push`. A release builds every row. The rows differ only in
   image and Qt package name, and a row-specific break fails the release
   before anything is published.
3. **The release notes are `tools/release-notes.md`** (`588d2d9`),
   filled in by `tools/release-notes.sh`, instead of a heredoc with
   every backtick escaped inside `release.yml`.
4. **The prune is a job of its own after Publish** (`d57890b`), so a
   failure there is a red job named for what failed beside a green
   Publish, not a red run named Publish.

Measured on the push that carried them:

| | before (this morning) | after |
|---|---|---|
| jobs per push | 17 | 12 |
| job time | about 1,865 s | 1,190 s |
| wall | 368 s | 368 s |

Wall did not move because it is bounded by the Windows build (254 s)
plus the installer job that needs it (108 s), and neither is packaging.
The job time is what runner minutes and queue contention are made of,
and it fell by more than a third.

Items 3 and 4 touch only `release.yml` and are exercised by the next
tag, not by this push; the notes script was run here and rendered both
lists with no token left over, and the `retire` job is the same script
call Publish made this afternoon, moved.

**2026-09-04, late — the sanitizer split and the single Windows compile.**
Two more from the fourth look, both measured on the runs that carried
them.

*The sanitizer job was four serial jobs wearing one name* (`a3d0c37`).
ASan over the matrices, UBSan over the matrices and the Qt suite, the
Qt suite under ASan, and the Swiss oracle each have a job now, with
their own dependencies and the comment that explains them. Dispatched:
eleven jobs green, **421 s wall against 959** for the release the night
before; ASan over the matrices is the long pole at 417 s, and a failure
would be named for the net that found it.

*The Windows job compiled the shared core twice for a difference that
was not there* (`9fe82d7`). `-DQTTEST` changes three files; its one
core-side effect, `AssertIndex()`, is an `assert()`, and the MSVC build
has passed `/DNDEBUG` since it was written -- so the 31 shared sources
compiled to the same object code both times, and the Windows test build
never had the range guard the Linux one (`Makefile.qt.test`, no NDEBUG)
has. Now: the 31 once into `obj\`, the three Qt sources once per binary,
two links. Measured on the push:

| | before | after |
|---|---|---|
| compile step(s) | 61 s + 55 s | 64 s |
| Windows build job | 254 s | 159 s |
| push lane wall | 368 s | 282 s |
| `astrolog.exe` | 2,061,824 bytes | 2,061,824 bytes |
| suite on Windows | 4543/0 | 4543/0 |

The byte-identical program is what identical core objects look like.
`qt-srcs.py --only core|qt|test` gives the split; the groups still come
from `Makefile.srcs`.

What is left of the fourth look's list is the two gates nobody can
falsify from here, the size of this record, a runner image GitHub will
retire, and six tags whose releases were pruned. None of it is a
change.

**2026-09-04, night — the warnings ledger is empty.** The maintainer
asked why the warnings were being kept rather than fixed, and the honest
answer was that each class had a verdict saying "not now" and nobody had
come back. So: fixed. The ledger held 113 lines that morning; what they
were and what was done is in the header of `tools/warnings.txt` and in
commit `5f2aa40`'s message. The numbers:

| | before | after |
|---|---|---|
| `-Wall` warning lines across the four GCC builds | 502 | 0 |
| ledger entries | 113 | 0 |
| the Qt6 ledger | 0 | 0 |
| suite | 4543/0 | 4543/0 |
| chart matrix vs the previous commit | | identical, 7,072 lines |
| influence matrix | | identical, 762 lines |
| switch matrix | | identical, 42,874 lines |
| graphics matrix | | identical, 455 checksums |

The vendored Swiss Ephemeris is exempted in `Makefile.srcs` rather than
edited -- `-Wno-write-strings -Wno-sign-compare` on its objects, with
`override` so the audit's command-line flags do not displace it -- and
that exemption removed the header-attributed sites too, since those
literals are instantiated only when the vendored sources are compiled.

What changes about the ledger's meaning: a new line in it is now a new
warning in the fork's own code, and the answer is to fix it rather than
to record it with a verdict. The `ubuntu-22.04` pin on the warnings job
was there to keep 113 known lines stable across compiler versions; with
nothing to keep stable, the pin's only remaining purpose is to stop a
newer g++'s new warnings from turning the job red -- which is now the
job's purpose. That is the next thing to try, and it is a CI change,
not a code one.

