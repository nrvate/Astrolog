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

# The distribution list lives in tools/distros.py and nowhere else; this
# used to be a case statement of its own, which is how it came to name
# fedora:42 for months after Fedora 42 stopped receiving updates.
#
# A suite the repository serves but the CURRENT matrix no longer builds
# -- a Fedora release that has aged out, whose packages older releases
# still carry -- is reported and skipped rather than verified. Its
# container image still exists, but its mirrors are on their way to the
# archive, and a check that fails for a reason nobody will act on is a
# check people stop reading. It is said out loud below, not hidden.
current=$(python3 tools/distros.py dists 2>/dev/null) || {
  echo "cannot determine the distribution list (tools/distros.py failed)"; exit 1; }
image_for() {
  case " $current " in
    *" $1 "*) python3 tools/distros.py image "$1" 2>/dev/null ;;
    *) echo '' ;;
  esac
}
aged_out() {
  case " $current " in
    *" $1 "*) return 1 ;;
  esac
  echo "$1: served, but no longer in the build matrix -- not verified"
  return 0
}

for dist in "$repo"/apt/dists/*/; do
  [ -d "$dist" ] || continue
  code=$(basename "$dist")
  aged_out "$code" && continue
  img=$(image_for "$code")
  [ -n "$img" ] || { echo "NO IMAGE MAPPED for apt suite '$code' -- add one to"
                     echo "tools/distros.py rather than leaving a suite untested."; fail=1; continue; }
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

  # And the UPGRADE path, which a fresh install cannot exercise. This is
  # where packaging actually breaks: a conffile prompt that hangs a
  # non-interactive run, a file that moved between versions, a maintainer
  # script that fails only when something is already installed. The
  # repository serves every release, so the path is real for every user
  # who installed before today.
  #
  # Oldest to newest, not version-to-version: one hop over the whole
  # history is the strongest single case, and it is what someone who
  # installed once and ran "apt upgrade" months later actually does.
  printf '%-12s %-38s ' "$code" "upgrade"
  out=$(docker run --rm -v "$repo":/repo:ro "$img" sh -c "
      export DEBIAN_FRONTEND=noninteractive
      apt-get update -qq >/dev/null 2>&1
      cp /repo/astrolog.gpg /usr/share/keyrings/
      echo 'deb [signed-by=/usr/share/keyrings/astrolog.gpg] file:///repo/apt $code main' \
        > /etc/apt/sources.list.d/astrolog.list
      apt-get update -qq >/dev/null 2>&1
      old=\$(apt-cache madison astrolog | awk -F'|' '{gsub(/ /,\"\",\$2); print \$2}' | tail -1)
      new=\$(apt-cache madison astrolog | awk -F'|' '{gsub(/ /,\"\",\$2); print \$2}' | head -1)
      [ \"\$old\" = \"\$new\" ] && { echo SINGLEVERSION; exit 0; }
      apt-get install -y -qq astrolog=\$old >/dev/null 2>&1 || { echo OLDFAILED; exit 1; }
      apt-get install -y -qq --only-upgrade astrolog >/dev/null 2>&1 || { echo UPGRADEFAILED; exit 1; }
      echo \"UPGRADED \$old -> \$(dpkg-query -W -f='\${Version}' astrolog)\"
      cd / && astrolog $chart
      # And removal, in the same container, so the third leg of the
      # package lifecycle costs nothing extra. A package that leaves files
      # behind on purge is a real defect and a policy violation, and
      # nothing here had ever removed one.
      apt-get purge -y -qq astrolog >/dev/null 2>&1 || { echo PURGEFAILED; exit 1; }
      for p in /usr/lib/astrolog /usr/bin/astrolog /usr/bin/astrolog-qt \
               /usr/share/applications/astrolog.desktop; do
        [ -e \"\$p\" ] && echo \"LEFTOVER \$p\"
      done
      find /usr/share/icons -name 'astrolog*' 2>/dev/null | sed 's/^/LEFTOVER /'
      echo PURGED" 2>&1) || {
    echo "UPGRADE FAILED"; printf '%s\n' "$out" | tail -6 | sed 's/^/    /'; fail=1; continue; }
  case $out in
    *SINGLEVERSION*) echo "skipped (only one version in the repository)"; continue ;;
  esac
  chiron=$(printf '%s\n' "$out" | grep -E '^Chir' | head -1 || true)
  case ${chiron:-none} in
    *0Ari00*|none)
      echo "BROKEN AFTER UPGRADE: ${chiron:-no chart}"
      printf '%s\n' "$out" | grep -E 'UPGRADED|FAILED' | sed 's/^/    /'; fail=1 ;;
    *) left=$(printf '%s\n' "$out" | grep '^LEFTOVER ' || true)
       if [ -n "$left" ]; then
         echo "FILES LEFT BEHIND AFTER REMOVAL:"
         printf '%s\n' "$left" | sed 's/^LEFTOVER /    /'; fail=1
       elif ! printf '%s\n' "$out" | grep -q '^PURGED$'; then
         echo "REMOVAL FAILED"; printf '%s\n' "$out" | tail -4 | sed 's/^/    /'; fail=1
       else
         echo "ok  $(printf '%s\n' "$out" | grep -o 'UPGRADED .*' || true), removed clean"
       fi ;;
  esac
done

for dist in "$repo"/rpm/*/; do
  [ -d "$dist" ] || continue
  d=$(basename "$dist")
  aged_out "$d" && continue
  img=$(image_for "$d")
  [ -n "$img" ] || { echo "NO IMAGE MAPPED for rpm dist '$d' -- add one to tools/distros.py."; fail=1; continue; }
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

  # The rpm upgrade path, for the same reason as the apt one above: a
  # fresh install cannot exercise it, and it is what every existing user
  # does. dnf reports the versions oldest-first, so head and tail are the
  # other way round from apt-cache madison.
  printf '%-12s %-38s ' "$d" "upgrade"
  out=$(docker run --rm -v "$repo":/repo:ro "$img" sh -c "
      rpm --import /repo/astrolog.asc
      printf '[astrolog]\nname=Astrolog\nbaseurl=file:///repo/rpm/$d\nenabled=1\ngpgcheck=1\nrepo_gpgcheck=1\ngpgkey=file:///repo/astrolog.asc\n' \
        > /etc/yum.repos.d/astrolog.repo
      vers=\$(dnf -y -q --disablerepo='*' --enablerepo=astrolog list --showduplicates astrolog 2>/dev/null \
              | awk '/^astrolog/ {print \$2}')
      old=\$(printf '%s\n' \"\$vers\" | head -1)
      new=\$(printf '%s\n' \"\$vers\" | tail -1)
      [ -z \"\$old\" ] && { echo NOVERSIONS; exit 1; }
      [ \"\$old\" = \"\$new\" ] && { echo SINGLEVERSION; exit 0; }
      dnf -y -q install astrolog-\$old >/dev/null 2>&1 || { echo OLDFAILED; exit 1; }
      dnf -y -q upgrade astrolog >/dev/null 2>&1 || { echo UPGRADEFAILED; exit 1; }
      echo \"UPGRADED \$old -> \$(rpm -q --qf '%{VERSION}-%{RELEASE}' astrolog)\"
      cd / && astrolog $chart
      dnf -y -q remove astrolog >/dev/null 2>&1 || { echo PURGEFAILED; exit 1; }
      for p in /usr/lib/astrolog /usr/bin/astrolog /usr/bin/astrolog-qt \
               /usr/share/applications/astrolog.desktop; do
        [ -e \"\$p\" ] && echo \"LEFTOVER \$p\"
      done
      find /usr/share/icons -name 'astrolog*' 2>/dev/null | sed 's/^/LEFTOVER /'
      echo PURGED" 2>&1) || {
    echo "UPGRADE FAILED"; printf '%s\n' "$out" | tail -6 | sed 's/^/    /'; fail=1; continue; }
  case $out in
    *SINGLEVERSION*) echo "skipped (only one version in the repository)"; continue ;;
  esac
  chiron=$(printf '%s\n' "$out" | grep -E '^Chir' | head -1 || true)
  case ${chiron:-none} in
    *0Ari00*|none)
      echo "BROKEN AFTER UPGRADE: ${chiron:-no chart}"
      printf '%s\n' "$out" | grep -E 'UPGRADED|FAILED' | sed 's/^/    /'; fail=1 ;;
    *) left=$(printf '%s\n' "$out" | grep '^LEFTOVER ' || true)
       if [ -n "$left" ]; then
         echo "FILES LEFT BEHIND AFTER REMOVAL:"
         printf '%s\n' "$left" | sed 's/^LEFTOVER /    /'; fail=1
       elif ! printf '%s\n' "$out" | grep -q '^PURGED$'; then
         echo "REMOVAL FAILED"; printf '%s\n' "$out" | tail -4 | sed 's/^/    /'; fail=1
       else
         echo "ok  $(printf '%s\n' "$out" | grep -o 'UPGRADED .*' || true), removed clean"
       fi ;;
  esac
done

[ "$fail" -eq 0 ] || { echo "== the repository cannot be installed from"; exit 1; }
echo "== every suite installs, upgrades, and computes a real chart"
