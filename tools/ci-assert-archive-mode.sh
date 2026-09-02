#!/bin/sh
# Assert a binary is still executable INSIDE the archive.
#
#   tools/ci-assert-archive-mode.sh astrolog-linux.tar.gz astrolog
#
# QT_CI_PLAN.md item 4.6. actions/upload-artifact does not preserve unix
# modes: a binary uploaded 0755 comes back 0644, and swisseph's first
# tagged release shipped a swetest that answered "Permission denied".
#
# The mode must be read OUT OF THE ARCHIVE, not off the disk. Checking the
# working copy tests the filesystem the archive was built from, which is
# never the thing that broke. That is the same mistake as asserting the
# Sun's position on a package with no ephemeris in it.
#
# This matters for the Linux package and not for the Windows one -- a .zip
# carries no unix modes and Windows does not want any. It is written now
# because the mechanism is what rots, and Q1 has not yet decided what the
# Linux artifact is.
set -eu

archive=${1:?usage: ci-assert-archive-mode.sh <archive.tar.gz> <path-in-archive>}
member=${2:?usage: ci-assert-archive-mode.sh <archive.tar.gz> <path-in-archive>}

[ -f "$archive" ] || { echo "NO ARCHIVE: $archive"; exit 1; }

line=$(tar tvzf "$archive" | grep -E "/?$member\$" | head -1 || true)
[ -n "$line" ] || {
  echo "NOT IN ARCHIVE: no member matching '$member' in $archive"
  echo "== members:"; tar tvzf "$archive" | head -5 | sed 's/^/  /'; exit 1; }

perms=${line%% *}
case $perms in
  *x*) echo "archive mode ok: $line" ;;
  *)   echo "NOT EXECUTABLE IN ARCHIVE: $line"
       echo "== The file is executable on disk and is not in the archive."
       echo "== A user who downloads this gets \"Permission denied\"."
       exit 1 ;;
esac
