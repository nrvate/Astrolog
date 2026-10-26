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
# works. (The container build scripts carried the dnf spelling of the
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
# A STALLED MIRROR, the third disease (2026-10-25): a release dry run's
# Linux job sat 24 minutes inside one download from the Azure mirror and
# was cancelled at its 25-minute limit with nothing wrong in the tree. So
# a transfer that stops is dropped after a minute and retried, and each
# whole attempt is bounded too; the archives already fetched stay in
# apt's cache, so a second attempt carries on rather than starting over.
printf 'Acquire::http::Timeout "60";\nAcquire::https::Timeout "60";\nAcquire::Retries "5";\n' \
  | sudo tee /etc/apt/apt.conf.d/99timeouts >/dev/null
sudo apt-get update
for attempt in 1 2 3; do
  if sudo timeout 480 apt-get install -y "$@"; then
    exit 0
  fi
  echo "ci-deps: apt-get install attempt $attempt did not finish; retrying"
done
echo "ci-deps: apt-get install failed three times"
exit 1
