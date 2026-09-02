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
keeps by remembering. A nightly job keeps it by existing.

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
| **The minimal ephemeris is already checked in** | `ephem/` holds `sepl_18.se1` (planets), `semo_18.se1` (Moon), `seas_18.se1` (main asteroids incl. Chiron) and 8 individual TNO short files — 2.1 MB, 1800–2400 |
| **`astrolog.as` already points at it** | `astrolog.as:63`, `-Yi1 "ephem"`. `nrvate.as:46` is what overrides to `/swe` |
| **19 of 39 bodies resolve from the bundled set — measured, not deduced** | `ASTROLOG_QT_TESTS=objsel ./astrolog-qt-test -Yi1 ephem` prints "19 of 39 bodies resolved and matched their listed name"; the same run with `-i nrvate.as` prints "39 of 39". The prediction from `run-qt-tests.sh`'s header and from the file list was exactly right, and is now retired in favour of the observation |
| **The suite does not degrade gracefully on the bundled set — it fails** | that same run is `FAIL: 61 passed, 2 failed`. Both failures are in the Object Selections group: `qttest.cpp:2535` and `:2537` type raw ephemeris number **52872 (Okyrhoe)** and require Lookup Names to resolve it. `/swe` has `se52872.se1`; `ephem/` does not |
| Making the bundled set pass costs 92 KB | `/swe/se52872.se1` is 93,806 bytes. Adding that one file clears both failures. Reaching 39/39 costs **7.9 MB** for 21 files |
| The 20 that do not resolve are cheap in absolute terms, not free | all exist in `/swe` and total **8.0 MB** (7.9 MB with Okyrhoe: 8,320,773 bytes); measured. **No short-file variant exists for any of them** — the `s`-suffixed short files in `ephem/` ship with Astrolog, not with the Swiss set |
| **Windows and Linux also render byte-identical graphics** | 86 of 109 sections identical outright; the other 23 are all output writers, and all but `-XM` reduce to CRLF plus an embedded output path. `-XM` (metafile) differs at byte 33 — unexplained |
| `graphics-matrix.sh` **used to exit 0 with renders missing, and no longer does** | it printed `== $runs renders, $missing produced no file` and set no status from it; seen live at 115 of 224 MISSING with `rc=0`. Fixed 2026-09-01 (work log item 163): it now prints `NOT A VALID BASELINE` and exits 1. **A CI step does not need to parse that line** — an earlier draft of this document told it to, which would have written a parser for a bug that was already gone |
| `graphics-matrix.sh` hard-depends on `nrvate.as` **and on `/swe`** | without the file every render reports `File 'nrvate.as' not found`; with it but on the bundled ephemeris, **115 of 224 renders fail** on `se07066s.se1` (Nessus). So this harness cannot run on `ephem/` alone — a direct argument for Q13 option B |
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
| **Three build configurations in this tree are compiled by nothing** | `Astrolog.vcxproj`; `WCLI` (19 conditional sites); `WSETUP` (10). Only `WIN` (`Makefile.win`) and `QT` (three makefiles) are actually built. This is the exact category swisseph's comments warn about, three times over |
| The Windows build has no console entry point | `main()` is inside `#ifndef WIN` (`astrolog.cpp:834`); the `WIN` build enters at `WinMain` (`wdriver.cpp:552`). So `astrolog.exe` cannot be smoke-tested non-interactively |
| `-ldl` is not dead code, but it is a no-op on modern Linux | `dladdr()` is genuinely called at `sweph.cpp:271`, under `__GNUC__`. glibc merged libdl into libc at 2.34, so `ldd ./astrolog-qt` shows no libdl at all on this box |
| **Fortify is already live in the ordinary builds** | `nm -D` counts 10 `*_chk` imports in `./astrolog` and 11 in `./astrolog-qt`, `__sprintf_chk` among them. The `-O0` ASan build has **1** — which measures `CLAUDE.md`'s claim that a fortify-detected overflow "cannot fire under it at all" |
| The Qt binary's dependency closure is large | 41 shared libraries, **~70 MB** measured with `du -Lch` over `ldd` output. `Qt5Network` alone drags in `libgssapi_krb5`, `libkrb5`, `libk5crypto`, `libcom_err`, `libkrb5support` |

### Not verified — settle these, don't assume them

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
| `Astrolog.vcxproj` | — | nothing | one `<ClCompile>` line behind (`switch.cpp`); targets `v142`, `Win32` only |
| ~~`WCLI`~~ | 19 | **`Makefile.wcli`, and CI** | **resolved 2026-09-02** — decided "build it in CI", which is one step in the existing Windows job and no new dependency. Guard widened at `astrolog.h:81`, `-mwindows` dropped, and it runs charts under Wine with no display (1.3b, 6.4b) |
| `WSETUP` | 10 | nothing | unknown |
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
**Status.** [ ]

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
**Status.** [x] A′ done 2026-09-02; **B still open** and still the thing
that would make CI as strong a net as a local run

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
**Status.** [ ]

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
**Status.** [ ]

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
**Status.** [ ]

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
**Status.** [ ]

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

**The guards that would stop it recurring are still open:**

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
**Status.** [ ]

### 3.3 The settings round trip
**Do.** `tools/settings-round-trip.sh` (needs `./astrolog` from 2.5).
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
**Caveat to write into the step.** This asserts the *mechanism* is
present, not that any particular overflow is caught. It guards a net; it
is not itself one.
**Status.** [ ]

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
**Status.** [ ]

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
**Status.** [ ]

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
**Status.** [ ]

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
**Status.** [ ]

### 4.6 Restore the executable bit
**Goal.** `actions/upload-artifact` does not carry unix modes. swisseph's
first tagged release shipped a `swetest` that answered "Permission
denied".
**Do.** `chmod 0755` the binaries after download, before archiving, and
read the mode back **out of the archive** (`tar tvzf`), not off the disk.
**Status.** [ ]

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

Nightly (`schedule:`) plus `workflow_dispatch`. Everything here is
documented as "pre-release rather than pre-commit"; CI is where that
promise becomes mechanical.

### 6.1 `tools/warning_audit.py`
**Do.** All four builds, diffed against `tools/warnings.txt`. Needs
qtbase5-dev, libx11-dev and mingw all present in one job.
**Why it matters here.** Its first run found the 62-commit Windows
breakage, and item 143's sweep left GCC naming every buffer still too
small for its worst case — two of which were live overflows (items
146–147).
**Status.** [ ]

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
**Status.** [ ]

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
documented invocation.
**Status.** [ ]

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

**A live question it raises.** Is DST autodetection genuinely
platform-dependent? If it is, two builds of this program disagree about
whether a 1991 June date is in daylight time, which is a parity bug worth
a work log item — and one no existing harness would have found, because
nothing compared the two builds' text output without a display before.
**Status.** [ ] — prototype proven; needs 1.3b's guard fix first

### 6.5 Graphics renders — move this to the fast lane
**Do.** `QTGRAPHDIR=<dir> ./astrolog-qt-test -i nrvate.as`, offscreen.
**Verified 2026-09-01:** **4.4 s**, 24 PNGs, and **24 distinct
checksums** — no two renders identical, so nothing is silently blank.
At four seconds this does not belong in a nightly lane; put it with
Phase 3. It still needs a pass criterion beyond "24 files appeared"
(see Q6's cousin: a committed checksum baseline would do it, and the
distinctness check above is a decent vacuity guard in the meantime).
**Status.** [ ]

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
**Status.** [ ]

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
**Status.** [ ]

### 7.4 Decide gate vs. report
**Q7.** A differential answers "something changed", not "something
broke", and `CLAUDE.md` is explicit that it "actively protects a wrong
answer, because fixing a 30-year-old bug shows up as a regression". A
hard gate would make every intentional behaviour change require a
workflow edit. Report-with-diff-attached, and a label or commit-trailer
opt-out for intentional changes, is probably right. Settle before
enabling.
**Status.** [ ]

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
**Status.** [ ] — recommendation: build it in CI

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

# Phase 10 — macOS: a shippable app — NOT DOING

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
- **A scheduled workflow switches itself off.** GitHub disables `schedule:`
  triggers in a repository with no activity for 60 days. Phase 6's whole
  argument is that a nightly job keeps a promise by existing — on a fork
  that goes quiet for two months it stops existing, silently, leaving a
  green history behind it. Keep `workflow_dispatch` on everything in that
  lane, and treat "the nightly has not run" as a condition somebody has to
  notice.
- **A third-party action pinned to a tag is a mutable dependency.** Pin
  the SHA and write the version beside it in a comment
  (`actions/checkout@11bd7190… # v4.2.2`), and resolve it with
  `git ls-remote` rather than from memory. Phases 1–3 need no third-party
  action at all, which is worth keeping true for as long as possible.
- **`tools/warning_audit.py` cannot be run with `--update` in CI**, and
  should not be. It flags *removed* warnings as well as new ones, on
  purpose, so a legitimate fix turns the job red until `tools/warnings.txt`
  is committed in the same change. That is correct behaviour and it is a
  workflow consequence: the baseline is part of the diff, not a thing CI
  maintains.
- **Never `git checkout` a file to undo a sabotage.** It reverts the
  file's entire share of the change. Twice in one session in this
  project. Reverse-patch the exact string.
- **The source is LF, and four categories are deliberately not.** This
  used to read "preserve CRLF", and it is the reverse now: work log item
  159 converted the tree, `.gitattributes` pins it with `* -text`, and
  `tools/line_endings_audit.py` fails on a carriage return in tracked
  source. What stays as it ships: binaries, files Windows or VMS tooling
  owns (`.rc`, `.sln`, `.def`, `makefile.com`), the third-party `font/`
  distribution, and **the data files the program parses** (`.as`,
  `.csv`) — converting those last moved `switch-matrix.sh` output by six
  lines, so something in the parsers reads a CR as content. A job that
  rewrites a tracked file must not touch those.
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
| Q1 | AppImage, tarball, or `.deb` for the Linux artifact? **Now costed**: an AppImage carries ~70 MB of closure; a `.deb` is ~8 MB and lets apt resolve Qt5. Still a judgement call about audience. | 4.2 |
| Q2 | What version scheme does this fork own, so a tag can be checked against source? | 5.1, 5.2 |
| ~~Q3~~ | ~~How does Phase 1 smoke-test a GUI-only `.exe`?~~ **Answered: build `WCLI` too.** One-line guard fix at `astrolog.h:81`, drop `-mwindows`, and a console Windows binary runs non-interactively under Wine. Verified 2026-09-01. | closed |
| ~~Q4~~ | ~~Does anything read `astexo.csv` at runtime?~~ **Answered: yes, it ships.** `charts3.cpp:1792`, `-XUx`. | closed |
| ~~Q5~~ | ~~An optimized `-g` build for the fortify class?~~ **Answered: no separate build needed.** The ordinary `-O` builds already import 10–11 `*_chk` symbols, so Phase 7's matrices — which run against `./astrolog`, a normal build — already cover it. New item 3.4 asserts it stays that way. | closed |
| Q6 | Pass criterion for the Windows-vs-Qt text diff? **Largely answered by 6.4b**: pin `-z0 0`, filter the one path-syntax diagnostic, and the Windows/Linux text matrix diffs to **zero** — 4 lines of 6,936, all cosmetic. Still open only for the *GUI* route in 6.4, which also captures layout. | 6.4, 6.4b |
| Q7 | Are the differentials a gate or a report, and how is an intentional change signalled? | 7.4 |
| Q8 | **Reopened 2026-09-02, as a different question.** It used to be "build Qt6 at all?"; a working Qt6 build landed the next day (`2582015`, suite 3561/0), so it is now **"who keeps it alive?"** Qt5 remains the supported and packaged configuration. Recommendation: one CI job, `apt-get install qt6-base-dev` + `make qt6 qt6-test` + the suite, ~15 s — or a comment at the site saying it is unbuilt. Not "supported". | 8.2 |
| ~~Q9~~ | ~~Is `-ldl` harmless on macOS?~~ **Answered: keep it on Linux, drop it on macOS.** `dladdr()` is really called (`sweph.cpp:271`); the flag is a no-op on glibc ≥ 2.34 but still needed below it, and macOS puts `dladdr` in libSystem. | closed |
| ~~Q10~~ | ~~Does this fork distribute signed macOS binaries at all?~~ **Answered 2026-09-01: no.** Phase 9 only — builds and assertions, no artifact. See Phase 10. | closed |
| Q11 | Repair `Astrolog.vcxproj` or delete it? **Now cheap either way**: it is exactly one `<ClCompile>` line behind. The real question is whether MSVC is a configuration this fork wants to owe anything to. See "The three unbuilt configurations". | — |
| ~~Q12~~ | ~~3526 or 2777?~~ **Answered: neither. Measured 3551 on 2026-09-01.** `2777` was written 2026-08-27 (`0a33328`), `3526` on 2026-08-31 (`2645464`); the suite has grown since. The lesson is not to pick a winner but to stop duplicating a number that rots by construction — the suite prints its own count, and three documents asserting it produced three wrong answers. | closed |
| ~~Q13~~ | ~~Minimal ephemeris only (A), check in the missing 8.0 MB (B), or fetch and cache in CI (C)?~~ **A′ done 2026-09-02**: `ephem/se52872.se1`, 93,806 bytes, so the bundled set passes with no gating and item 2.3 loses its special case. **B remains open as a cheap follow-up** — 8.0 MB buys CI the same 39/39 net the maintainer has, and until it is taken CI is a weaker net than a local run. The mode gets built either way. | 2.3 |

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
