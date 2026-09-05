#!/bin/sh
# Every CI assertion script, fed input it must refuse -- and, where that
# is cheap, input it must accept.
#
#   tools/ci-selftest.sh              # seconds; needs ./astrolog for two
#                                     # cases and ./astrolog-qt for the
#                                     # install round trip, skips them
#                                     # otherwise and says so
#   SELFTEST_ONLINE=1 tools/ci-selftest.sh   # also the gh-backed scripts
#
# Why this exists. Twenty-two tools/ci-*.sh scripts gate pushes and
# releases, and the record that each was falsified lived in
# QT_CI_PLAN.md prose. On 2026-09-05 ci-assert-green.sh's "no run found"
# path turned out to be wrong the first time it was reached -- a path a
# script takes only when something has already gone wrong is exactly the
# path nobody has watched. This makes the falsification a check that
# runs on every push instead of a memory: for each script, a known-bad
# input must exit nonzero and a known-good one must exit zero. A script
# that accepts the bad input is the finding.
#
# What it cannot cover, and why: the scripts that need Wine
# (ci-verify-windows-installer.sh, ci-verify-windows-starts.sh), a
# network (ci-verify-published-release.sh, and ci-assert-green.sh under
# SELFTEST_ONLINE) or apt (ci-deps.sh). Each is falsified where it runs,
# and their bad paths that need none of that are here.
set -u
cd "$(dirname "$0")/.."
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
fail=0; n=0; skipped=0

expect() {  # expect <0|fail> <label> <command...>
  want=$1; label=$2; shift 2
  out=$("$@" 2>&1); rc=$?
  n=$((n + 1))
  if [ "$want" = 0 ] && [ "$rc" -eq 0 ]; then echo "ok    $label"
  elif [ "$want" != 0 ] && [ "$rc" -ne 0 ]; then echo "ok    $label (exit $rc)"
  else
    [ "$want" = 0 ] && w="0" || w="nonzero"
    echo "WRONG $label -- exit $rc, expected $w"
    printf '%s\n' "$out" | sed 's/^/      /' | head -6
    fail=1
  fi
}
skip() { echo "skip  $1"; skipped=$((skipped + 1)); }

# ci-assert-version.sh: the tree's own tag passes, any other fails.
ver=$(tools/ci-assert-version.sh 2>/dev/null || true)
if [ -n "$ver" ]; then
  expect 0    "version: accepts the tree's own tag v$ver" tools/ci-assert-version.sh "v$ver"
else
  echo "WRONG version: cannot read the tree's version"; fail=1
fi
expect fail "version: refuses a tag that disagrees with astrolog.h" tools/ci-assert-version.sh v0.00-qt.0
expect fail "version: refuses an empty tag" tools/ci-assert-version.sh v

# ci-assert-fresh.sh: newer than every source passes, older fails.
touch "$T/new"; touch -d 2000-01-01 "$T/old"
expect 0    "fresh: a file newer than every source" tools/ci-assert-fresh.sh "$T/new"
expect fail "fresh: a file older than the sources" tools/ci-assert-fresh.sh "$T/old"
expect fail "fresh: a file that does not exist" tools/ci-assert-fresh.sh "$T/missing"

# ci-assert-distinct.sh: distinct files at the expected count pass.
mkdir -p "$T/d"; echo a > "$T/d/1"; echo b > "$T/d/2"
expect 0    "distinct: two different files, count 2" tools/ci-assert-distinct.sh "$T/d" 2
expect fail "distinct: the wrong count" tools/ci-assert-distinct.sh "$T/d" 3
cp "$T/d/1" "$T/d/3"
expect fail "distinct: two identical files" tools/ci-assert-distinct.sh "$T/d"
expect fail "distinct: a directory that does not exist" tools/ci-assert-distinct.sh "$T/nodir"
mkdir -p "$T/empty"
expect fail "distinct: an empty directory" tools/ci-assert-distinct.sh "$T/empty"

# ci-assert-fortify.sh: the console binary imports *_chk; demanding more
# than any binary has must fail.
if [ -x ./astrolog ]; then
  expect 0    "fortify: ./astrolog imports at least 10 *_chk" tools/ci-assert-fortify.sh ./astrolog 10
  expect fail "fortify: ./astrolog against an impossible minimum" tools/ci-assert-fortify.sh ./astrolog 1000000
else
  skip "fortify: no ./astrolog here"
fi
expect fail "fortify: a binary that does not exist" tools/ci-assert-fortify.sh "$T/nobinary" 1

# ci-assert-archive-mode.sh: an executable member passes; 644 fails.
mkdir -p "$T/a"; printf 'x' > "$T/a/prog"; chmod 755 "$T/a/prog"
tar czf "$T/ok.tgz" -C "$T" a
chmod 644 "$T/a/prog"; tar czf "$T/bad.tgz" -C "$T" a
expect 0    "archive-mode: an executable member" tools/ci-assert-archive-mode.sh "$T/ok.tgz" a/prog
expect fail "archive-mode: a member without the exec bit" tools/ci-assert-archive-mode.sh "$T/bad.tgz" a/prog
expect fail "archive-mode: a member that is not in the archive" tools/ci-assert-archive-mode.sh "$T/ok.tgz" a/other
expect fail "archive-mode: an archive that does not exist" tools/ci-assert-archive-mode.sh "$T/none.tgz" a/prog

# ci-assert-clang-clean.sh: a warning in our sources fails; one in the
# vendored Swiss files counts and passes; more than expected fails; a
# filename spliced by a parallel build resolves to the real file.
printf 'calc.cpp:12:3: warning: unused variable\n' > "$T/ours.log"
printf 'swemplan.cpp:882:11: warning: sprintf is deprecated\n' > "$T/vendored.log"
printf 'aswemplan.cpp:882:11: warning: sprintf is deprecated\n' > "$T/spliced.log"
expect fail "clang-clean: a warning in calc.cpp" tools/ci-assert-clang-clean.sh "$T/ours.log"
expect 0    "clang-clean: one vendored warning under a bound of 5" tools/ci-assert-clang-clean.sh "$T/vendored.log" 5
expect fail "clang-clean: one vendored warning over a bound of 0" tools/ci-assert-clang-clean.sh "$T/vendored.log" 0
expect 0    "clang-clean: a spliced vendored filename still counts as vendored" tools/ci-assert-clang-clean.sh "$T/spliced.log" 5
expect fail "clang-clean: a log that does not exist" tools/ci-assert-clang-clean.sh "$T/nolog"

# ci-assert-desktop.sh / installed / uninstalled: an empty prefix has no
# entry and nothing to remove; a leftover fails the uninstall check.
# "Uninstalled" means our files are gone and the SHARED directories are
# still there: an uninstall that removed $prefix/bin would be a bug.
mkdir -p "$T/p0/bin" "$T/p0/share/applications" "$T/p0/share/icons/hicolor/48x48/apps"
expect fail "desktop: a prefix with no desktop entry" tools/ci-assert-desktop.sh "$T/p0"
expect fail "installed: a wrapper that does not exist" tools/ci-assert-installed.sh "$T/p0/bin/astrolog"
expect 0    "uninstalled: shared directories present, our files gone" tools/ci-assert-uninstalled.sh "$T/p0"
mkdir -p "$T/p1/bin"; : > "$T/p1/bin/astrolog"
expect fail "uninstalled: a prefix with a leftover wrapper" tools/ci-assert-uninstalled.sh "$T/p1"
expect fail "uninstalled: a prefix whose shared directories are gone" tools/ci-assert-uninstalled.sh "$T/p2"

# The install round trip, when both binaries are here: install into a
# scratch prefix, assert it, uninstall, assert that too.
if [ -x ./astrolog ] && [ -x ./astrolog-qt ]; then
  if make install PREFIX="$T/inst" >/dev/null 2>&1; then
    expect 0 "installed: a real wrapper, run from /" tools/ci-assert-installed.sh "$T/inst/bin/astrolog"
    expect 0 "desktop: a real install" tools/ci-assert-desktop.sh "$T/inst"
    expect fail "uninstalled: before make uninstall" tools/ci-assert-uninstalled.sh "$T/inst"
    make uninstall PREFIX="$T/inst" >/dev/null 2>&1
    expect 0 "uninstalled: after make uninstall" tools/ci-assert-uninstalled.sh "$T/inst"
  else
    echo "WRONG install: make install PREFIX=$T/inst failed"; fail=1
  fi
else
  skip "install round trip: needs ./astrolog and ./astrolog-qt"
fi

# ci-verify-package.sh: a tree with every required file and a manifest
# that covers them passes; an empty tree, a missing data file, a
# forbidden file and a stale manifest each fail.
stage() {  # stage <dir>: the required payload as empty files, manifest written.
  # Every .se1 the tree ships, by name, because the check counts them
  # against ephem/ -- a package with three of thirty-two is what it is for.
  mkdir -p "$1/ephem" "$1/font"
  for f in ephem/*.se1; do : > "$1/$f"; done
  for f in astrolog.as atlas.as timezone.as sefstars.txt seorbel.txt astexo.csv \
           earth.bmp astrolog.htm changes.htm license.htm astrolog.exe; do : > "$1/$f"; done
  (cd "$1" && find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS)
}
stage "$T/pkg"
expect 0    "verify-package: the required payload with its manifest" tools/ci-verify-package.sh "$T/pkg"
expect fail "verify-package: an empty directory" tools/ci-verify-package.sh "$T/empty"
expect fail "verify-package: not a directory" tools/ci-verify-package.sh "$T/new"
stage "$T/pkg2"; rm "$T/pkg2/ephem/seas_18.se1"
expect fail "verify-package: the Chiron ephemeris missing" tools/ci-verify-package.sh "$T/pkg2"
stage "$T/pkg3"; : > "$T/pkg3/nrvate.as"
expect fail "verify-package: a forbidden file (nrvate.as) present" tools/ci-verify-package.sh "$T/pkg3"
stage "$T/pkg4"; echo changed > "$T/pkg4/astrolog.as"
expect fail "verify-package: a file changed after the manifest" tools/ci-verify-package.sh "$T/pkg4"

# ci-verify-zip.sh: the archive must unpack to exactly the staged tree.
if command -v zip >/dev/null 2>&1; then
  mkdir -p "$T/z"; stage "$T/z/astrolog-x"
  (cd "$T/z" && zip -qr ../good.zip astrolog-x)
  expect 0    "verify-zip: a zip of the staged tree" tools/ci-verify-zip.sh "$T/good.zip" "$T/z/astrolog-x"
  echo changed > "$T/z/astrolog-x/atlas.as"
  expect fail "verify-zip: the staged tree changed after zipping" tools/ci-verify-zip.sh "$T/good.zip" "$T/z/astrolog-x"
  mkdir -p "$T/z2/one" "$T/z2/two"; : > "$T/z2/one/f"; : > "$T/z2/two/f"
  (cd "$T/z2" && zip -qr ../two.zip one two)
  expect fail "verify-zip: a zip with two top-level directories" tools/ci-verify-zip.sh "$T/two.zip" "$T/z2/one"
  expect fail "verify-zip: a zip that does not exist" tools/ci-verify-zip.sh "$T/none.zip" "$T/z/astrolog-x"
else
  skip "verify-zip: no zip(1) here"
fi

# ci-verify-release-dist.sh: exactly the count, every name carrying the
# tree's version, no strays, no tilde.
vdeb=$(echo "$ver" | sed 's/-qt\./+qt./')
mkdir -p "$T/r"; : > "$T/r/astrolog-$ver.el9.x86_64.rpm"; : > "$T/r/astrolog_$vdeb.noble_amd64.deb"
expect 0    "release-dist: two artifacts of this version, count 2" tools/ci-verify-release-dist.sh "$T/r" 2
expect fail "release-dist: two artifacts, count 3" tools/ci-verify-release-dist.sh "$T/r" 3
: > "$T/r/notes.txt"
expect fail "release-dist: a stray file" tools/ci-verify-release-dist.sh "$T/r" 2
rm -f "$T/r/notes.txt"; : > "$T/r/astrolog_$vdeb~jammy_amd64.deb"
expect fail "release-dist: a tilde in an artifact name" tools/ci-verify-release-dist.sh "$T/r" 3
rm -f "$T/r/astrolog_$vdeb~jammy_amd64.deb"; : > "$T/r/astrolog-0.00-qt.0.el9.x86_64.rpm"
expect fail "release-dist: an artifact from another version" tools/ci-verify-release-dist.sh "$T/r" 3
expect fail "release-dist: a directory that does not exist" tools/ci-verify-release-dist.sh "$T/nodist" 1

# ci-assert-green.sh: a commit no run exists for must fail, once the
# appearance window (0 here) has passed. Needs gh and a network.
if [ "${SELFTEST_ONLINE:-0}" = 1 ]; then
  expect fail "green: a commit CI never saw" env CI_GREEN_APPEAR=0 GITHUB_REPOSITORY=nrvate/Astrolog \
    tools/ci-assert-green.sh 0000000000000000000000000000000000000000
else
  skip "green: SELFTEST_ONLINE=1 to run the gh-backed case"
fi

echo "== $n cases, $skipped skipped"
[ "$fail" -eq 0 ] || { echo "SELFTEST FAILED: a CI assertion script accepted input it must refuse (or refused good input)"; exit 1; }
echo "selftest clean: every assertion script refuses what it must"
