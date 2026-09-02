# Astrolog 8.00 — with a Qt GUI for Linux

This is a fork of Astrolog 8.00 (released May 31, 2026), described at
http://www.astrolog.org/astrolog.htm and originally copied from
http://www.astrolog.org/ftp/ast80src.zip — the changes upstream made in
8.00 are listed at http://www.astrolog.org/ftp/updat800.htm

Everything Astrolog does, and all of its chart calculation and drawing
code, is Walter D. Pullen's work. See LICENSE.HTM.

## What this fork adds

A **Qt GUI backend for Linux**, with the menus and dialogs the Windows
build has. Upstream's Linux build uses X11 directly and is driven by
single-keystroke commands; this one gives you the same nine menus and
~28 dialogs a Windows user would recognise, so the two builds can be
used more or less interchangeably.

The port itself changes almost nothing in the shared calculation or
rendering core, and where it must, it does so inside `#ifdef QT`. The new
code lives in two files, `qtdriver.cpp` (main window, canvas, menu bar)
and `qtdialog.cpp` (dialogs), which stand in for the Windows-only
`wdriver.cpp`/`wdialog.cpp`. The backend is selected with `-DQT` on the
compile line, following the same pattern astrolog.h already used for its
`X11`/`WIN`/`WCLI` choice, so the existing builds are unaffected.

Separately, the fork adds a few things to **both** builds — a new Object
Selections dialog, and fixes for settings the program was silently
failing to save. Those deliberately carry no `QT` guard, because they go
into `astrolog.rc`, `wdialog.cpp` and the shared code the way upstream
would take them. See "Features this fork adds to both builds" in
`QT_GUI_PLAN.md`.

## Installing a package

Releases carry native packages, built on the distribution they target and
installed into a clean container before publishing, so the dependency
list comes from the binary rather than from someone's memory:

| | |
|---|---|
| `.deb` | Ubuntu 22.04 (jammy), 24.04 (noble) |
| `.rpm` | Fedora 42, Fedora 43, EL9 (Rocky/Alma), EL10 |
| `.zip` | Windows, statically linked — unpack and run |

### From the repository (upgrades arrive on their own)

```sh
# Debian / Ubuntu
sudo curl -fsSL -o /usr/share/keyrings/astrolog.gpg \
  https://nrvate.github.io/Astrolog/astrolog.gpg
. /etc/os-release
echo "deb [signed-by=/usr/share/keyrings/astrolog.gpg] \
  https://nrvate.github.io/Astrolog/apt ${UBUNTU_CODENAME:-$VERSION_CODENAME} main" \
  | sudo tee /etc/apt/sources.list.d/astrolog.list
sudo apt update && sudo apt install astrolog
```

```sh
# Fedora (use rpm/el$releasever instead on RHEL/Rocky/Alma)
sudo tee /etc/yum.repos.d/astrolog.repo <<'EOF'
[astrolog]
name=Astrolog
baseurl=https://nrvate.github.io/Astrolog/rpm/fc$releasever
enabled=1
gpgcheck=1
repo_gpgcheck=1
gpgkey=https://nrvate.github.io/Astrolog/astrolog.asc
EOF
sudo dnf install astrolog
```

<https://nrvate.github.io/Astrolog/> has the same instructions, generated
from the repository itself so they cannot drift from its layout. Each
distribution gets its own suite: one suite holding everything makes apt
and dnf offer the highest-versioned package rather than the one built for
your release.

### Or a single file

```sh
sudo apt install ./astrolog_8.00+qt.1.jammy_amd64.deb   # apt resolves Qt
sudo dnf install ./astrolog-8.00-qt.1.el9.x86_64.rpm
```

The package's own version is `8.00+qt.1~jammy`, with a tilde, so that a
22.04 build sorts below a 24.04 one and an upgrade is an upgrade. GitHub
rewrites `~` to `.` in release asset filenames, which is why the
downloaded file does not match; `apt` reads the version out of the
package, not out of the name.

Both `astrolog` (command line) and `astrolog-qt` (windowed) land on
`PATH`. The binaries live in `/usr/lib/astrolog` beside their data —
Astrolog reads its ephemeris, atlas and fonts from the directory of its
own executable — and `/usr/bin` holds wrappers, which is the same
arrangement `make install` uses.

**EL10 is built against Qt6** because it ships no Qt5 outside EPEL, and
depending on a third-party repository for a runtime library is a poor
thing to put in a package. Everything else is Qt5.

Versions are `8.00-qt.N`: upstream numbers the program, this numbers the
port.

## Building


```
make qt          # ./astrolog-qt
make install     # on PATH; PREFIX=$HOME/.local if you don't want root
```

**Qt5 and Qt6 are both supported, and one build covers both**: the
makefile asks `pkg-config` which is present and builds against the better
one, so `make qt` is the right command on either. Needs the development
package (`qtbase5-dev` or `qt6-base-dev` on Debian/Ubuntu/Mint) and
`pkg-config` — the build stops and names the package if they are
missing. Object files go to `obj-qt/`, so this can be
built alongside the regular `astrolog` binary without interfering with it:
plain `make` still builds the stock X11 version, and `make all` builds
everything this tree has.

`make install` deliberately leaves the data where it is — the ephemeris
files, the atlas, the fonts, `astrolog.as` — and installs small wrappers
that run the in-tree binary, along with a menu entry and icons for the Qt
build. The checkout therefore has to stay put; re-run `make install` if you
move it, or `make uninstall` to remove what it wrote.

## Status

Feature complete against the Windows build. All nine menus, all 258 menu
items, every dialog, all 42 right-click context menus and 264 keyboard
shortcuts are implemented, and each dialog has been read field-by-field
against its Windows counterpart — labels, field order, number formatting,
dropdown contents, and which menu checkmarks it refreshes on OK. Menu
mnemonics sit on the same letters Windows uses, so Alt-key muscle memory
carries over. Charts animate, print, paste, run all 96 macro slots, and
draw with Astrolog's bundled astrology fonts. Text charts render in the
main window on a character grid, as they do on Windows, rather than in a
separate text box.

It also goes slightly *past* the Windows build in one place: an Object
Selections dialog (Ctrl+T) that puts a chosen body or a midpoint into any
Uranian or Dwarf slot, which neither build previously offered — added to
both, not just this one.

A handful of smaller things are either deliberately different from
Windows or deliberately left out — mostly Win32-only settings with no
Linux meaning, and a few places where Windows' own behaviour looks like a
bug and this build does the sensible thing instead. They're all listed
under "Known divergences from Windows" in `QT_GUI_PLAN.md` rather than
left for you to discover.

## Appearance

The Qt build follows the desktop's light/dark setting. Qt 5 has no API for
this — `QStyleHints::colorScheme()` arrived in Qt 6.5 — and Qt 5's own gtk3
platform theme loads without supplying a palette, so Astrolog reads the
preference itself: the `org.freedesktop.appearance` portal first, which is
what Qt 6.5 reads too, then GNOME/Cinnamon/MATE via `gsettings`, XFCE via
`xfconf-query`, then `kdeglobals`, `gtk-3.0/settings.ini` and `GTK_THEME`.
The last three need no helper programs installed.

Set `ASTROLOG_QT_THEME=dark` or `=light` to override the detection:

```sh
ASTROLOG_QT_THEME=light ./astrolog-qt
```

## Tests

```
make qt-test
./run-qt-tests.sh
```

A headless suite of over 3,500 assertions plus startup checks — it prints
its own count, which is the only number that stays right — covering dialogs,
context menus, shortcuts, chart rendering, every menu item, menu parity
against `astrolog.rc`, and bad input. Several groups drive the real
dialogs and assert what they leave behind, rather than calling the code
underneath them. No X display needed; exits nonzero on failure.

One group answers a different kind of question. Everything else here is
*differential* — it can tell you an answer changed, never that it is
right. The **numeric oracle** asks the Swiss Ephemeris library the same
question Astrolog asks it, through an object mapping written out
independently in the test file, and requires the same answer: exact
agreement over 15 bodies and seven epochs from 1900 to 2080. It also
cross-checks Astrolog's own built-in planetary formulas against Swiss,
and requires all 40 house systems to divide the circle once. It found two
shared-core bugs on the day it was written.

Four byte-diff harnesses prove a change to shared code left behaviour
alone, and they cover disjoint surfaces. `tools/chart-matrix.sh` renders
every text chart the console build draws over a pinned date;
`tools/switch-matrix.sh` covers the whole command-line switch surface in
529 invocations; `tools/graphics-matrix.sh` covers the drawing code, which
neither of the others reaches, in 224 renders; `tools/influence-matrix.sh`
covers the influence charts. Run any of them against an older build of the
tree and diff.

It defaults to `-i nrvate.as`, the maintainer's settings file, because
that is the only input under which the esoteric bodies resolve at all —
without it a fifth of the checks skip themselves silently.

The Windows build has a small suite of its own, driving the real binary
under Wine:

```
tools/win-tests.sh
```

It takes minutes rather than seconds, so it is run when a change ships in
both builds rather than before every commit.

There are also eight standing audits — four checking this port against
Windows' resource script, one checking the compiled defaults against the
shipped settings file, one checking the switch registry against the help
text, one checking that every ranged switch has a round-trip fixture, and
one checking line endings:

```
python3 tools/rc_audit.py          # dialog controls nothing wires up
python3 tools/rc_mnemonic_audit.py # "&" placement vs the .rc
python3 tools/rc_field_audit.py    # a control wired to the wrong setting
python3 tools/rc_lookup_audit.py   # a by-name lookup resolving to nothing
python3 tools/defaults_audit.py    # data.cpp initializers vs astrolog.as
python3 tools/registry_audit.py    # documented switches that resolve nowhere
python3 tools/fixture_coverage_audit.py  # ranged switches with no fixture
python3 tools/line_endings_audit.py      # a carriage return in the source
```

And the compiler itself: `tools/warning_audit.py` holds all five builds
against a ledger of every warning they are known to produce, and fails on
an addition.

The dialog tables, accelerators and command IDs are *generated* from the
resource script rather than transcribed, and regenerating them into a
`diff` is how you check they are still in sync:

```
python3 tools/rc2qt.py astrolog.rc | diff - qtrcdlg.h
```

## Comparing against the real Windows build

`Makefile.win` cross-compiles the actual Windows binary with mingw-w64 —
same `wdriver.cpp`, same `wdialog.cpp`, same `astrolog.rc` — so it can be
run under Wine and compared side by side rather than reasoned about:

```
make -f Makefile.win
wine ./astrolog.exe
```

There's a scripted comparison too, which captures the same text charts
from both builds and stacks them for a look:

```
tools/text-chart-capture.sh out/win
QTTEXTDIR=out/qt ./run-qt-tests.sh
python3 tools/text-chart-diff.py out/win out/qt out/cmp
```

Needs `g++-mingw-w64-x86-64`, `wine`, `xvfb`, `metacity`, `xdotool`,
`imagemagick` and `python3-pil`. See `QT_COMPARING_WITH_WINDOWS.md`.

## Docs

- **`CLAUDE.md`** — orientation for picking the work up from a fresh
  clone: build and test commands, the audits, the Wine reference build,
  and the rules this repo is worked under.
- **`QT_GUI_PLAN.md`** — read this first if you're picking the work up.
  How the Qt backend fits into Astrolog's architecture, the gotchas worth
  knowing before changing it, a "What to do next" section, per-menu
  status, a log of every item done and what it actually turned out to be,
  and every place this port knowingly differs from Windows.
- **`QT_TESTING.md`** — how to answer a question about the program in
  about a fifth of a second, render any chart to a PNG with no display,
  and the traps that waste hours if you drive a window instead.
- **`QT_COMPARING_WITH_WINDOWS.md`** — how to build the real Windows
  binary, run it under Wine, and diff it against this port.
- **`QT_MENU_MAPPING.md`** — the Windows menu structure extracted from
  `astrolog.rc`, with command IDs. The reference the Qt menu bar is
  built against.
- **`REFACTORING.md`** — the standing architectural review: the designs
  that make this codebase hard to evolve, each with evidence and with the
  incident where it drew blood, plus what has been done about them.
- **`CONVENTIONS.md`** — the conventions the code actually follows,
  verified and written down: naming, index domains and their "none"
  values, the macro families, the buffer and error idioms, and how to add
  a command. Read it before writing new code.
