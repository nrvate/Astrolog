#!/bin/sh
# Install from a built repository, in a clean container, per distribution.
#
#   tools/ci-verify-repo.sh public
#
# QT_CI_PLAN.md item 4.7. This exists because the first published
# repository was perfect by every other measure and could not be
# installed from: it built, it signed, every URL returned 200, and on
# Ubuntu 22.04 apt offered the noble package and refused it. Nothing
# short of running the install would have shown it, and nothing in the
# workflow was running the install.
#
# So the workflow runs it now, BEFORE deploying to Pages, over every
# suite the repository claims to serve. A repository that cannot be
# installed from is worse than no repository: it is advertised.
#
# The suites are discovered from the tree rather than listed here, so a
# distribution added to the build matrix cannot be silently left
# untested -- the same reason SHA256SUMS counts its own lines.
set -eu

repo=${1:?usage: ci-verify-repo.sh <repo dir>}
repo=$(cd "$repo" && pwd)
command -v docker >/dev/null || { echo "need docker"; exit 1; }

chart='-qa 6 15 1990 12:00 0 122W19 47N36 -R1 _X'
fail=0

image_for() {
  case $1 in
    jammy)  echo ubuntu:22.04 ;;
    noble)  echo ubuntu:24.04 ;;
    fc42)   echo fedora:42 ;;
    fc43)   echo fedora:43 ;;
    el9)    echo quay.io/rockylinux/rockylinux:9 ;;
    el10)   echo quay.io/rockylinux/rockylinux:10 ;;
    *)      echo '' ;;
  esac
}

for dist in "$repo"/apt/dists/*/; do
  [ -d "$dist" ] || continue
  code=$(basename "$dist")
  img=$(image_for "$code")
  [ -n "$img" ] || { echo "NO IMAGE MAPPED for apt suite '$code' -- add one to"
                     echo "image_for() rather than leaving a suite untested."; fail=1; continue; }
  printf '%-12s %-38s ' "$code" "$img"
  out=$(docker run --rm -v "$repo":/repo:ro "$img" sh -c "
      export DEBIAN_FRONTEND=noninteractive
      apt-get update -qq >/dev/null 2>&1
      cp /repo/astrolog.gpg /usr/share/keyrings/
      echo 'deb [signed-by=/usr/share/keyrings/astrolog.gpg] file:///repo/apt $code main' \
        > /etc/apt/sources.list.d/astrolog.list
      apt-get update -qq >/dev/null 2>&1
      apt-get install -y -qq astrolog >/dev/null 2>&1
      cd / && astrolog $chart" 2>&1) || { echo "INSTALL FAILED"; printf '%s\n' "$out" | tail -6 | sed 's/^/    /'; fail=1; continue; }
  chiron=$(printf '%s\n' "$out" | grep -E '^Chir' | head -1 || true)
  case ${chiron:-none} in
    *0Ari00*|none) echo "BAD: ${chiron:-no chart}"; fail=1 ;;
    *) echo "ok" ;;
  esac
done

for dist in "$repo"/rpm/*/; do
  [ -d "$dist" ] || continue
  d=$(basename "$dist")
  img=$(image_for "$d")
  [ -n "$img" ] || { echo "NO IMAGE MAPPED for rpm dist '$d'."; fail=1; continue; }
  printf '%-12s %-38s ' "$d" "$img"
  out=$(docker run --rm -v "$repo":/repo:ro "$img" sh -c "
      rpm --import /repo/astrolog.asc
      printf '[astrolog]\nname=Astrolog\nbaseurl=file:///repo/rpm/$d\nenabled=1\ngpgcheck=1\nrepo_gpgcheck=1\ngpgkey=file:///repo/astrolog.asc\n' \
        > /etc/yum.repos.d/astrolog.repo
      dnf install -y -q astrolog >/dev/null 2>&1
      cd / && astrolog $chart" 2>&1) || { echo "INSTALL FAILED"; printf '%s\n' "$out" | tail -6 | sed 's/^/    /'; fail=1; continue; }
  chiron=$(printf '%s\n' "$out" | grep -E '^Chir' | head -1 || true)
  case ${chiron:-none} in
    *0Ari00*|none) echo "BAD: ${chiron:-no chart}"; fail=1 ;;
    *) echo "ok" ;;
  esac
done

[ "$fail" -eq 0 ] || { echo "== the repository cannot be installed from"; exit 1; }
echo "== every suite installs and computes a real chart"
