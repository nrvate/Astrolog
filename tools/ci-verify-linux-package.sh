#!/bin/sh
# Install a Linux package in a clean container and prove it works there.
#
#   tools/ci-verify-linux-package.sh out/package/astrolog_*.deb ubuntu:22.04
#   tools/ci-verify-linux-package.sh out/package/astrolog-*.rpm fedora:42
#
# QT_CI_PLAN.md item 4.5, for native packages. The point is not that the
# file exists -- it is that a machine which has never seen this source
# tree can install it, resolve its dependencies from its own repositories,
# and get right answers.
#
# ASSERT A BODY THAT CANNOT COMPUTE WITHOUT THE EPHEMERIS. Astrolog falls
# back to the Moshier formulas in silence: with the ephemeris files
# entirely absent the Sun still reads 24Gem07'46" and only its velocity
# moves, in the seventh decimal. A "^Sun +24Gem07" check therefore passes
# on a package containing no ephemeris at all -- which is the assertion
# this project specified for two drafts before anyone removed the files
# and looked. Chiron reads 0Ari00'00" when the data is missing, which is
# binary and unmistakable.
#
# It runs from "/" on purpose. Astrolog resolves its data from the
# directory of its own executable, so running it from the directory it was
# built in tests nothing about the install.
set -eu

pkg=${1:?usage: ci-verify-linux-package.sh <package> <image>}
image=${2:?usage: ci-verify-linux-package.sh <package> <image>}
[ -f "$pkg" ] || { echo "NO PACKAGE: $pkg"; exit 1; }

pkgdir=/pkg
[ "${2:-}" = local ] && pkgdir=$(cd "$(dirname "$pkg")" && pwd)
case $pkg in
  *.deb) install='apt-get update -qq && apt-get install -y -qq '"$pkgdir/$(basename "$pkg")" ;;
  *.rpm) install='dnf install -y -q '"$pkgdir/$(basename "$pkg")" ;;
  *) echo "unknown package type: $pkg"; exit 2 ;;
esac

# The chart is pinned, and _X is mandatory: without it the console build
# tries to open an X window and there is none.
chart='-qa 6 15 1990 12:00 0 122W19 47N36 -R1 _X'

# The body of the check, run either in a fresh container or right here.
check="set -e
    cd /
    command -v astrolog >/dev/null || { echo 'NO-WRAPPER'; exit 1; }
    ldd /usr/lib/astrolog/astrolog-qt | grep -q 'not found' && { echo 'UNRESOLVED-QT-DEPS'; ldd /usr/lib/astrolog/astrolog-qt | grep 'not found'; exit 1; }
    astrolog $chart"

if [ "$image" = local ]; then
  # For the RPM job, which already runs INSIDE the distribution's
  # container and so cannot start one of its own. A weaker test than a
  # clean image -- the build dependencies are already present, so it
  # proves less about Requires -- but it still exercises the thing most
  # likely to be wrong, which is whether the program finds its data from
  # /usr/lib/astrolog rather than from the directory it was built in.
  out=$(sh -c "$install >/dev/null 2>&1; $check" 2>&1) || {
    echo "PACKAGE FAILED locally:"; printf '%s\n' "$out" | tail -15; exit 1; }
  image="this container"
else
  out=$(docker run --rm -v "$(cd "$(dirname "$pkg")" && pwd)":/pkg:ro "$image" \
    sh -c "$install >/dev/null 2>&1
    $check" 2>&1) || {
    echo "PACKAGE FAILED on $image:"; printf '%s\n' "$out" | tail -15; exit 1; }
fi

chiron=$(printf '%s\n' "$out" | grep -E '^Chir' | head -1 || true)
[ -n "$chiron" ] || {
  echo "NO CHART on $image -- the program ran but printed no Chiron line:"
  printf '%s\n' "$out" | head -8 | sed 's/^/  /'; exit 1; }

case $chiron in
  *0Ari00*)
    echo "EPHEMERIS NOT FOUND on $image: $chiron"
    echo "0Ari00'00\" is Astrolog's no-ephemeris answer. The package installed"
    echo "and ran, and could not find its own data files."
    exit 1 ;;
esac

echo "$image ok: installed from a clean image, ran from /, $chiron"
