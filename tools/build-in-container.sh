#!/bin/sh
# Build and package this tree inside a distribution's own container.
#
#   tools/build-in-container.sh <image> <qt-dev-package> deb|rpm [outdir]
#
#   tools/build-in-container.sh ubuntu:26.04 qt6-base-dev deb out/package
#   tools/build-in-container.sh fedora:44 qt6-qtbase-devel rpm out/package
#
# Why a container and not a runner image. The .deb jobs used to run ON
# GitHub's ubuntu-22.04 and ubuntu-24.04 runner images, which made the
# Ubuntu matrix a list of whatever images GitHub happened to offer:
# Ubuntu 26.04 LTS shipped in April 2026 and no ubuntu-26.04 runner
# existed five months later. The rpm jobs never had that problem,
# because they run inside "container: fedora:44" -- a Docker image that
# exists the day a release does. This does the same for .deb, and does
# it as a script so the build can be reproduced on any machine with
# Docker, which is how the Fedora 44 row was proven before it was
# wired in and how the Ubuntu 26.04 row was proven the same way.
#
# Why a script and not "container:" in the job. A job that runs inside
# the container cannot start a second, CLEAN container to prove the
# package installs there, so the rpm jobs verify with the weaker "local"
# mode -- the build dependencies are already present, so a wrong
# Requires line is invisible. Running Docker from the host keeps the
# strong check: tools/ci-verify-linux-package.sh installs the result
# into a fresh image afterwards, resolving Qt from the distribution's
# own repositories, and asks it for Chiron.
#
# The tree is mounted read-write, because make writes object files into
# it; the object directories are the same ones a host build uses, so run
# "make clean" between a container build and a host build of a different
# distribution, or they will share stale objects. The container's root
# owns what it writes; the last step hands it back to the caller.
set -eu

image=${1:?usage: build-in-container.sh <image> <qt-dev-package> deb|rpm [outdir]}
qt=${2:?usage: build-in-container.sh <image> <qt-dev-package> deb|rpm [outdir]}
kind=${3:?usage: build-in-container.sh <image> <qt-dev-package> deb|rpm [outdir]}
out=${4:-out/package}
cd "$(dirname "$0")/.."
command -v docker >/dev/null || { echo "docker not found"; exit 2; }

case $kind in
  deb) deps="g++ make git pkg-config libx11-dev dpkg-dev $qt"
       install='export DEBIAN_FRONTEND=noninteractive
         echo "Acquire::ForceIPv4 \"true\";" > /etc/apt/apt.conf.d/99force-ipv4
         apt-get update -qq >/dev/null
         apt-get install -y -qq --no-install-recommends '"$deps"' >/dev/null'
       package="tools/package-deb.sh $out" ;;
  rpm) deps="gcc-c++ make git rpm-build pkgconf-pkg-config libX11-devel $qt"
       install='echo ip_resolve=4 >> /etc/dnf/dnf.conf
         printf "timeout=20\nretries=2\nfastestmirror=False\n" >> /etc/dnf/dnf.conf
         dnf install -y -q dnf-plugins-core >/dev/null 2>&1 || true
         (dnf config-manager --set-enabled crb || dnf config-manager --enable crb || true) >/dev/null 2>&1
         dnf install -y -q --setopt=install_weak_deps=False '"$deps"' >/dev/null'
       package="tools/package-rpm.sh $out" ;;
  *) echo "kind must be deb or rpm, not '$kind'"; exit 2 ;;
esac

# PKG_REV/PKG_SHA are computed HERE, on the host, where git can read the
# repository; inside the container the checkout is a bind mount owned by
# another uid and git refuses it. PKG_RELEASE passes through as given, so
# release.yml's "PKG_RELEASE=1" means the same thing it always did.
rev=$(git log -1 --format=%cd --date=format:%Y%m%d 2>/dev/null || date +%Y%m%d)
sha=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)

echo "== $image: build both binaries and package as $kind"
docker run --rm -v "$PWD:/src" -w /src \
  -e "PKG_REV=$rev" -e "PKG_SHA=$sha" -e "PKG_RELEASE=${PKG_RELEASE:-}" \
  "$image" sh -ec "
    $install
    make -j4 >/dev/null
    make qt -j4 >/dev/null
    $package
    chown -R $(id -u):$(id -g) $out obj-qt 2>/dev/null || true
    find . -maxdepth 1 -user 0 -exec chown $(id -u):$(id -g) {} + 2>/dev/null || true
  "
ls -l "$out"/*."$kind"
