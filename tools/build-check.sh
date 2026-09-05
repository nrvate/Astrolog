#!/bin/sh
# Does this tree build, from a clean checkout, on a popular distribution?
#
#   tools/build-check.sh                 # every image below
#   tools/build-check.sh fedora:44 arch  # just these
#   tools/build-check.sh --list
#
# Linux users build from source: there are no .deb or .rpm packages any
# more (2026-09-05, the maintainer's decision). So the thing that has to
# work is "install these packages, then make" -- on whatever the user
# happens to run. This checks exactly that, in a container of each
# distribution, from a git archive of HEAD rather than the working tree,
# so nothing already built here can make it pass.
#
# It builds ./astrolog and ./astrolog-qt, then casts a chart with the
# console one and requires Chiron to be a real position -- 0Ari00 means
# the ephemeris was not found, which is what a broken data path looks
# like rather than a broken build.
#
# The package names per distribution are the useful output: they are what
# README.md tells a user to install, and they are wrong often enough that
# guessing them is not good enough.
set -eu
cd "$(dirname "$0")/.."
command -v docker >/dev/null || { echo "docker not found"; exit 2; }

# image | one shell line installing a compiler, make, pkg-config, X11 and Qt
recipe() {
  case $1 in
    ubuntu:22.04|ubuntu:24.04|debian:12)
      echo 'export DEBIAN_FRONTEND=noninteractive; apt-get update -qq && apt-get install -y -qq --no-install-recommends g++ make pkg-config libx11-dev qtbase5-dev' ;;
    ubuntu:26.04|debian:13)
      echo 'export DEBIAN_FRONTEND=noninteractive; apt-get update -qq && apt-get install -y -qq --no-install-recommends g++ make pkg-config libx11-dev qt6-base-dev' ;;
    fedora:*)
      echo 'dnf install -y -q --setopt=install_weak_deps=False gcc-c++ make pkgconf-pkg-config libX11-devel qt6-qtbase-devel' ;;
    *rockylinux:9)
      echo 'dnf install -y -q dnf-plugins-core >/dev/null 2>&1; (dnf config-manager --set-enabled crb || dnf config-manager --enable crb) >/dev/null 2>&1; dnf install -y -q --setopt=install_weak_deps=False gcc-c++ make pkgconf-pkg-config libX11-devel qt5-qtbase-devel' ;;
    *rockylinux:10)
      echo 'dnf install -y -q dnf-plugins-core >/dev/null 2>&1; (dnf config-manager --set-enabled crb || dnf config-manager --enable crb) >/dev/null 2>&1; dnf install -y -q --setopt=install_weak_deps=False gcc-c++ make pkgconf-pkg-config libX11-devel qt6-qtbase-devel' ;;
    archlinux*)
      echo 'pacman -Sy --noconfirm --needed gcc make pkgconf libx11 qt6-base >/dev/null' ;;
    opensuse/*)
      echo 'zypper -n --gpg-auto-import-keys refresh >/dev/null && zypper -n install -y gcc-c++ make pkgconf-pkg-config libX11-devel qt6-base-devel >/dev/null' ;;
    alpine*)
      echo 'apk add --no-cache g++ make pkgconf libx11-dev qt6-qtbase-dev >/dev/null' ;;
    *) echo '' ;;
  esac
}

IMAGES='ubuntu:22.04 ubuntu:24.04 ubuntu:26.04 debian:12 debian:13
        fedora:43 fedora:44 quay.io/rockylinux/rockylinux:9
        quay.io/rockylinux/rockylinux:10 archlinux:latest
        opensuse/tumbleweed alpine:3.22'

[ "${1:-}" != "--list" ] || { echo $IMAGES; exit 0; }
[ $# -eq 0 ] || IMAGES="$*"

src=$(mktemp -d); trap 'rm -rf "$src"' EXIT
git archive HEAD | tar -x -C "$src"
echo "== source: git archive HEAD ($(git rev-parse --short HEAD)), $(find "$src" -name '*.cpp' | wc -l) sources"

fail=0
for img in $IMAGES; do
  rec=$(recipe "$img")
  [ -n "$rec" ] || { echo "$img: no recipe -- add one to tools/build-check.sh"; fail=1; continue; }
  printf '%-40s ' "$img"
  out=$(docker run --rm -v "$src":/src:ro -w /work "$img" sh -ec "
      cp -r /src/. /work/
      $rec
      make astrolog -j4 >/tmp/b1.log 2>&1 || { echo CONSOLE_BUILD_FAILED; tail -15 /tmp/b1.log; exit 1; }
      make qt -j4      >/tmp/b2.log 2>&1 || { echo QT_BUILD_FAILED;      tail -15 /tmp/b2.log; exit 1; }
      ./astrolog -qa 6 15 1990 12:00 0 122W19 47N36 -R1 _X -Yi1 ephem | grep -E '^Chir' | head -1
    " 2>&1) || { echo "FAILED"; printf '%s\n' "$out" | sed 's/^/    /' | tail -16; fail=1; continue; }
  chiron=$(printf '%s\n' "$out" | grep -E '^Chir' | head -1 || true)
  case ${chiron:-none} in
    *0Ari00*|none) echo "BUILT, BAD CHART: ${chiron:-none}"; fail=1 ;;
    *) echo "ok   $chiron" ;;
  esac
done
[ $fail -eq 0 ] || { echo "== at least one distribution cannot build this tree"; exit 1; }
echo "== every distribution built both binaries and computed Chiron"
