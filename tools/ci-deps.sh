#!/bin/sh
# Install apt packages on a GitHub runner, the way every job here does it.
#
#   tools/ci-deps.sh [--i386] <package>...
#
# This is the one piece of CI that was still written inline, and it was
# written fifteen times: the same three lines -- force IPv4, update,
# install -- in every job of every workflow, five of them carrying the
# same six-line comment about why. A change to how packages are
# installed was fifteen edits. The package LIST stays in the job, where
# what a job needs is worth seeing; the mechanism lives here.
#
# WHY FORCE IPv4. The el9 dnf install sat for seven minutes waiting out
# IPv6 mirror attempts, and on 2026-09-02 an apt-get on an Ubuntu runner
# did the same for fourteen. Same disease, other package manager: the
# runner advertises IPv6 that does not route to the mirrors, and every
# attempt waits out its timeout. Forcing IPv4 costs nothing where IPv6
# works. (tools/build-in-container.sh carries the dnf spelling of the
# same fix, with the numbers that produced it.)
#
# --i386 adds the architecture first, for Wine: wine32 is an i386
# package and apt will not see it otherwise.
set -eu

i386=""
if [ "${1:-}" = "--i386" ]; then i386=1; shift; fi
[ $# -ge 1 ] || { echo "usage: ci-deps.sh [--i386] <package>..."; exit 2; }

if [ -n "$i386" ]; then
  sudo dpkg --add-architecture i386
fi
echo 'Acquire::ForceIPv4 "true";' | sudo tee /etc/apt/apt.conf.d/99force-ipv4 >/dev/null
sudo apt-get update
sudo apt-get install -y "$@"
