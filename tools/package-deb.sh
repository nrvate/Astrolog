#!/bin/sh
# Build a .deb from a staged tree.
#
#   tools/package-deb.sh [outdir]
#
# QT_CI_PLAN.md item 4.2. Built ON the distribution it targets, never
# cross-built: a .deb records a dependency on the Qt and glibc SONAMEs it
# was linked against, and those differ between Ubuntu releases. CI runs
# this on ubuntu-22.04 and ubuntu-24.04 runners for that reason, which is
# the same lesson as swisseph's glibc scar in a different coat.
#
# DEPENDENCIES ARE COMPUTED, NOT WRITTEN DOWN. dpkg-shlibdeps reads the
# actual ELF NEEDED entries of both binaries and asks dpkg which packages
# own those SONAMEs. Hand-writing "libqt5widgets5, libqt5gui5, ..." would
# be a list that is wrong the first time Qt is linked differently and
# silently wrong afterwards -- the same class as every hardcoded count
# this project has already been bitten by.
set -eu

out=${1:-out/package}
cd "$(dirname "$0")/.."
root=$(pwd)
# Absolute, because dpkg-shlibdeps is run from a scratch directory below
# and a relative path stops resolving the moment we cd. This is exactly
# how the first CI run failed while the same script passed here: the
# local invocation happened to pass an absolute outdir and the workflow
# passed "out/package".
mkdir -p "$out"
out=$(cd "$out" && pwd)

command -v dpkg-deb >/dev/null || { echo "need dpkg-deb (dpkg-dev)"; exit 1; }
command -v dpkg-shlibdeps >/dev/null || { echo "need dpkg-shlibdeps (dpkg-dev)"; exit 1; }

ver=$(sed -n 's/.*#define szVersionCore "\([^"]*\)".*/\1/p' astrolog.h | head -1)
[ -n "$ver" ] || { echo "cannot read szVersionCore from astrolog.h"; exit 1; }
# Q2 answered: astrolog.h owns szVersionFork, so a release is 8.00+qt.N
# and dpkg orders it after a bare 8.00 ("-" would be read as a Debian
# revision separator, which is why it is "+"). Untagged builds keep the
# date and commit after it, so a package built from a branch can never be
# mistaken for the release of the same number.
# PKG_REV and PKG_SHA can be supplied by the caller, because this runs
# inside a container in CI and a git worktree's ".git" is a FILE pointing
# at a path outside the mount -- git there fails with "not a git
# repository: (null)". The workflow passes them in from the checkout.
rev=${PKG_REV:-$(git log -1 --format=%cd --date=format:%Y%m%d 2>/dev/null || date +%Y%m%d)}
sha=${PKG_SHA:-$(git rev-parse --short HEAD 2>/dev/null || echo unknown)}
fork=$(sed -n 's/^#define szVersionFork "\([^"]*\)".*/\1/p' astrolog.h | head -1)
[ -n "$fork" ] || { echo "cannot read szVersionFork from astrolog.h"; exit 1; }
if [ "${PKG_RELEASE:-}" = 1 ]; then
  pkgver="$ver+qt.$fork"
else
  pkgver="$ver+qt.$fork~$rev.$sha"
fi
arch=$(dpkg --print-architecture)

stage="$out/deb-stage"
tools/package-stage.sh "$stage" /usr >/dev/null
mkdir -p "$stage/DEBIAN"

# dpkg-shlibdeps wants to be run from a directory containing debian/control
# and writes its answer into debian/substvars. Give it exactly that, in a
# scratch dir, rather than pretending this is a full debian/ source tree.
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/debian"
: > "$tmp/debian/control"
# Its stderr is kept and printed on failure. The first draft sent it to
# /dev/null and the first CI failure said only "dpkg-shlibdeps failed",
# which is the same self-inflicted blindness as discarding Wine's stderr
# in the Windows differential. Warnings on the happy path are expected --
# it says "binaries to analyze should already be installed in their
# package's directory", which is true and harmless here.
( cd "$tmp" && dpkg-shlibdeps -O --ignore-missing-info \
    "$stage/usr/lib/astrolog/astrolog" \
    "$stage/usr/lib/astrolog/astrolog-qt" 2>"$tmp/err" ) > "$tmp/deps" || {
  echo "dpkg-shlibdeps failed -- refusing to guess dependencies:"
  sed 's/^/  /' "$tmp/err"; exit 1; }
depends=$(sed -n 's/^shlibs:Depends=//p' "$tmp/deps")
[ -n "$depends" ] || { echo "dpkg-shlibdeps produced no dependencies -- that"
  echo "cannot be right for a dynamically linked Qt program; refusing."; exit 1; }

size=$(du -ks "$stage/usr" | cut -f1)

cat > "$stage/DEBIAN/control" <<EOF
Package: astrolog
Version: $pkgver
Section: science
Priority: optional
Architecture: $arch
Depends: $depends
Installed-Size: $size
Maintainer: nrvate <nrvate@gmail.com>
Homepage: https://github.com/nrvate/Astrolog
Description: Astrology chart calculator with a Qt GUI
 Astrolog casts and displays astrological charts, with the Swiss Ephemeris
 for planetary positions and a bundled ephemeris covering 1800-2400.
 .
 This is the Qt/Linux port of Astrolog 8.00, which gives the Linux build
 the same menus and dialogs the Windows build has. Both a command line
 program (astrolog) and a windowed one (astrolog-qt) are installed.
 .
 The program reads its data files from the directory of its own
 executable, so the binaries live in /usr/lib/astrolog beside them and
 /usr/bin holds wrappers.
EOF

mkdir -p "$out"
deb="$out/astrolog_${pkgver}_${arch}.deb"
dpkg-deb --build --root-owner-group "$stage" "$deb" >/dev/null
echo "built $deb ($(du -h "$deb" | cut -f1))"
echo "Depends: $depends"
