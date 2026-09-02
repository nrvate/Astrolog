#!/bin/sh
# Build an .rpm from a staged tree.
#
#   tools/package-rpm.sh [outdir]
#
# QT_CI_PLAN.md item 4.2, the RPM half. Run this ON the distribution it
# targets -- in CI that is a Fedora container -- for the same reason the
# .deb is built on each Ubuntu release: the package records the SONAMEs it
# was linked against, and those differ.
#
# A NOTE ON "LAST TWO LTS". Fedora has no LTS; each release is supported
# for about thirteen months, so "the last two Fedora releases" is a list
# that moves every six months and lives in the workflow, not here. The
# genuinely long-supported RPM targets are the Enterprise Linux rebuilds
# (Rocky, Alma) at EL9 and EL10, which is a separate decision because
# their Qt5 story is not Fedora's.
#
# REQUIRES ARE COMPUTED, NOT WRITTEN. rpmbuild reads the ELF NEEDED
# entries and generates Requires from them, the same job dpkg-shlibdeps
# does on the Debian side. Nothing here lists a Qt package by hand.
set -eu

out=${1:-out/package}
cd "$(dirname "$0")/.."
root=$(pwd)

command -v rpmbuild >/dev/null || {
  echo "need rpmbuild (rpm-build). This is meant to run inside the"
  echo "distribution's own container -- see .github/workflows/ci.yml."
  exit 1; }

ver=$(sed -n 's/.*#define szVersionCore "\([^"]*\)".*/\1/p' astrolog.h | head -1)
[ -n "$ver" ] || { echo "cannot read szVersionCore from astrolog.h"; exit 1; }
# Q2 is still open, so the release field carries the fork's identity:
# upstream's version, then a date and commit. RPM forbids "-" in either
# field, which is why this is not the Debian string.
# PKG_REV and PKG_SHA can be supplied by the caller, because this runs
# inside a container in CI and a git worktree's ".git" is a FILE pointing
# at a path outside the mount -- git there fails with "not a git
# repository: (null)". The workflow passes them in from the checkout.
rev=${PKG_REV:-$(git log -1 --format=%cd --date=format:%Y%m%d 2>/dev/null || date +%Y%m%d)}
sha=${PKG_SHA:-$(git rev-parse --short HEAD 2>/dev/null || echo unknown)}
rel="qt$rev.$sha"

top=$(mktemp -d)
trap 'rm -rf "$top"' EXIT
mkdir -p "$top/BUILD" "$top/RPMS" "$top/SPECS"
stage="$top/stage"
tools/package-stage.sh "$stage" /usr >/dev/null

# The file list is generated from what was actually staged, so a file
# added to the payload cannot be silently left out of the package -- the
# same reason the Windows manifest counts its own lines.
( cd "$stage" && find . -type f -o -type l ) | sed 's|^\.||' > "$top/files.list"
( cd "$stage" && find . -type d -path './usr/lib/astrolog*' ) | sed 's|^\.||' \
  | sed 's|^|%dir |' >> "$top/files.list"

cat > "$top/SPECS/astrolog.spec" <<EOF
# Prebuilt binaries are staged by tools/package-stage.sh, so there is
# nothing for rpmbuild to compile and no debuginfo to split out.
%global debug_package %{nil}
%global __brp_check_rpaths %{nil}

Name:           astrolog
Version:        $ver
Release:        $rel%{?dist}
Summary:        Astrology chart calculator with a Qt GUI
License:        GPL-2.0-or-later
URL:            https://github.com/nrvate/Astrolog

%description
Astrolog casts and displays astrological charts, using the Swiss Ephemeris
for planetary positions with a bundled ephemeris covering 1800-2400.

This is the Qt/Linux port of Astrolog 8.00, which gives the Linux build the
same menus and dialogs the Windows build has. It installs a command line
program (astrolog) and a windowed one (astrolog-qt).

The program reads its data files from the directory of its own executable,
so the binaries live in %{_prefix}/lib/astrolog beside them and
%{_bindir} holds wrappers.

%install
cp -a $stage/* %{buildroot}/

%files -f $top/files.list

%changelog
* $(LC_ALL=C date '+%a %b %d %Y') nrvate <nrvate@gmail.com> - $ver-$rel
- Built from $sha by tools/package-rpm.sh
EOF

rpmbuild --define "_topdir $top" --define "_buildrootdir $top/BUILDROOT" \
         -bb "$top/SPECS/astrolog.spec" >"$top/build.log" 2>&1 || {
  echo "rpmbuild failed:"; tail -25 "$top/build.log"; exit 1; }

mkdir -p "$out"
found=$(find "$top/RPMS" -name '*.rpm' | head -1)
[ -n "$found" ] || { echo "rpmbuild produced no .rpm"; tail -20 "$top/build.log"; exit 1; }
cp "$found" "$out/"
rpm_out="$out/$(basename "$found")"
echo "built $rpm_out ($(du -h "$rpm_out" | cut -f1))"
echo "Requires: $(rpm -qp --requires "$rpm_out" 2>/dev/null | grep -iE 'qt|libc\.so|libX11' | tr '\n' ' ')"
