#!/bin/sh
# Does the PUBLISHED repository actually serve this version?
#
#   tools/ci-verify-live-repo.sh [version] [base-url]
#
# tools/ci-verify-repo.sh checks the repository that repo.yml just built,
# in the job that built it, before it is deployed. That is the right place
# for most of it -- it installs from the tree on every distribution it
# serves. What it cannot check is the half that happens afterwards: the
# Pages deploy. A deploy can succeed and serve the previous commit, and
# every check upstream of it stays green.
#
# So this asks the only question that matters to a user: fetch the index
# over HTTPS the way apt and dnf will, and require the version to be in
# it, for every distribution.
#
# NOT a CI gate, deliberately. Pages propagation is not instant and a
# check that is retried until it passes is not a check. Run it after a
# release, by hand, once the deploy has settled.
set -eu

ver=${1:-$(tools/ci-assert-version.sh 2>/dev/null || echo "")}
base=${2:-https://nrvate.github.io/Astrolog}
[ -n "$ver" ] || { echo "usage: ci-verify-live-repo.sh <version> [base-url]"; exit 2; }
command -v curl >/dev/null || { echo "curl not found"; exit 2; }

# Three spellings of one version, because each packaging format insists
# on its own and none of them is negotiable:
#
#   .deb       8.00+qt.3~jammy     dpkg reads "-" as the Debian revision
#   .rpm file  8.00-qt.3.fc42      name-version-release
#   .rpm META  ver="8.00" rel="qt.3.fc42"
#
# That last one is the one that caught this script out. RPM does not store
# a version string at all: primary.xml splits it into separate attributes,
# so "8.00-qt.3" appears nowhere in the metadata and grepping for it
# reported a perfectly good repository as broken on all four rpm
# distributions while apt passed.
debver=$(echo "$ver" | sed 's/-qt\./+qt./')
rpmver=${ver%%-qt.*}                       # 8.00
rpmrel=qt.${ver##*-qt.}                    # qt.3
fail=0
get() { curl -sSfL --max-time 40 "$1" 2>/dev/null; }

echo "== $base, looking for $ver"

# The list of what must be served: DISTS in the environment if set,
# otherwise tools/distros.py, the one place the matrix is defined. Its
# Fedora rows move twice a year; a list written here would say fc42
# until somebody noticed.
#
# DISTS exists because the two questions differ. "Does the site serve
# the version I just released, for every distribution that release was
# built for" is answered by passing the distributions read off the
# release's own asset names -- the slow lane's "published" job does
# that. "For every distribution the matrix names TODAY" is the default,
# and is the wrong question the day after a Fedora release lands: it
# fails on the new one until the next tag, for a reason nobody can act
# on, which is the fastest way to teach people to ignore a check.
dists=${DISTS:-$(python3 tools/distros.py dists 2>/dev/null)} || true
[ -n "$dists" ] || {
  echo "cannot determine the distribution list (tools/distros.py failed and DISTS is unset)"; exit 2; }
for code in $dists; do
  case $code in fc*|el*) continue ;; esac
  url="$base/apt/dists/$code/main/binary-amd64/Packages"
  body=$(get "$url" || true)
  if [ -z "$body" ]; then
    echo "   FAIL: apt/$code -- no Packages index at $url"; fail=1; continue
  fi
  case $body in
    *"$debver~$code"*) echo "   ok: apt/$code serves $debver~$code" ;;
    *) echo "   FAIL: apt/$code has no $debver~$code. It offers:"
       printf '%s\n' "$body" | grep '^Version:' | sed 's/^/        /'
       fail=1 ;;
  esac
done

for dist in $dists; do
  case $dist in fc*|el*) ;; *) continue ;; esac
  md=$(get "$base/rpm/$dist/repodata/repomd.xml" || true)
  if [ -z "$md" ]; then
    echo "   FAIL: rpm/$dist -- no repomd.xml"; fail=1; continue
  fi
  # The primary index, whose href repomd.xml names. Not guessed: the
  # filename is checksum-prefixed and changes every rebuild.
  # The ATTRIBUTE VALUE, not the tag. The first version of this took the
  # whole 'location href="..."/' string and handed curl a URL with a quote
  # in it, which fails as "bad/illegal format" and looks like a missing
  # file.
  href=$(printf '%s' "$md" | tr '<>' '\n\n' | grep -m1 'primary\.xml' \
         | sed -n 's/.*href="\([^"]*\)".*/\1/p' || true)
  if [ -z "$href" ]; then
    echo "   FAIL: rpm/$dist -- repomd.xml names no primary.xml"; fail=1; continue
  fi
  body=$(get "$base/rpm/$dist/$href" | gzip -cd 2>/dev/null || true)
  case $body in
    *"ver=\"$rpmver\" rel=\"$rpmrel.$dist\""*)
      echo "   ok: rpm/$dist serves $rpmver-$rpmrel.$dist" ;;
    *) echo "   FAIL: rpm/$dist has no $rpmver-$rpmrel.$dist. It offers:"
       printf '%s' "$body" | grep -oE '<version[^/]*/>' | sed 's/^/        /'
       fail=1 ;;
  esac
done

[ $fail -eq 0 ] || { echo "THE PUBLISHED REPOSITORY IS NOT SERVING $ver."; exit 1; }
echo "live repository ok: every distribution serves $ver"
