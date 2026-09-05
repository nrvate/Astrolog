### Installing

**Linux** — `.deb` for Ubuntu @UBUNTUS@, `.rpm` for Fedora @FEDORAS@
and EL9/EL10. Or use the [signed repository](https://nrvate.github.io/Astrolog/)
so upgrades arrive on their own.

**Windows** — run `astrolog-<version>-setup.exe`. It installs to
Program Files, adds a Start Menu entry and an Add/Remove Programs
row, and uninstalls cleanly. It is **not** code-signed, so
SmartScreen will show "Windows protected your PC" — choose *More
info*, then *Run anyway*. If you would rather not run an
installer at all, the `.zip` is the same files: unpack it
anywhere and run `astrolog.exe`. This is the Qt build of
Astrolog, the same program as on Linux and macOS, with its
runtime included; it needs **Windows 10 or later**.

**macOS** (12 or later, Apple Silicon) — the app is ad-hoc signed but not *notarized*, so a
download through a browser is quarantined and macOS says it is
damaged. It is not. Either right-click the app and choose
**Open**, or run once:

```sh
xattr -dr com.apple.quarantine /Applications/Astrolog.app
```

A download fetched with `curl` is never quarantined and just runs.

---
