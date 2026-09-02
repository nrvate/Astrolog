#!/bin/sh
# Build signed apt and dnf repositories from a directory of packages.
#
#   tools/make-repo.sh <package-dir> <output-dir> [base-url]
#
# QT_CI_PLAN.md item 4.7. Release assets make a user download a file and
# install it by hand, once per release. A repository makes it
# "apt install astrolog" and then "apt upgrade" forever, which is most of
# what packaging is for.
#
# LAYOUT, and both halves are flat on purpose -- there is one component
# and a handful of packages, so a pool hierarchy would be ceremony:
#
#   apt/dists/<codename>/Release{,.gpg}  InRelease
#   apt/dists/<codename>/main/binary-amd64/Packages{,.gz}
#   apt/pool/<codename>/*.deb
#   rpm/<dist>/repodata/repomd.xml{,.asc}
#   rpm/<dist>/*.rpm
#   astrolog.gpg      the public key, BINARY, for apt signed-by=
#   astrolog.asc      the same key ARMOURED, for dnf gpgkey=
#
# ONE SUITE PER DISTRIBUTION. The first draft used a single suite holding
# everything, on the theory that the codename in each version was enough
# to tell them apart. It is not, and the very first install from the live
# repository proved it -- see the comment on the layout code below.
#
# SIGNING is optional here and required in CI. Without a key this builds
# an unsigned repository, which is useful for testing and useless to
# publish: apt refuses it and dnf has to be told to skip gpgcheck, which
# is a bad habit to teach. The workflow passes a key; a local run usually
# will not.
set -eu

pkgdir=${1:?usage: make-repo.sh <package-dir> <output-dir> [base-url]}
out=${2:?usage: make-repo.sh <package-dir> <output-dir> [base-url]}
baseurl=${3:-}

command -v apt-ftparchive >/dev/null || { echo "need apt-ftparchive (apt-utils)"; exit 1; }
command -v createrepo_c >/dev/null || { echo "need createrepo_c"; exit 1; }

pkgdir=$(cd "$pkgdir" && pwd)
rm -rf "$out"
mkdir -p "$out"

ndeb=$(find "$pkgdir" -name '*.deb' | wc -l)
nrpm=$(find "$pkgdir" -name '*.rpm' | wc -l)
[ "$ndeb" -gt 0 ] || { echo "no .deb in $pkgdir -- refusing to publish an empty repository"; exit 1; }
[ "$nrpm" -gt 0 ] || { echo "no .rpm in $pkgdir -- refusing to publish an empty repository"; exit 1; }

# ONE SUITE PER DISTRIBUTION, and the first draft got this wrong in a way
# that only a real install could show. A single suite holding every
# package means apt sees 8.00+qt.1~jammy and 8.00+qt.1~noble as two
# versions of one package and offers the HIGHEST -- so an Ubuntu 22.04
# box was offered the noble build and refused it:
#
#   Depends: libqt5core5t64 (>= 5.15.1) but it is not installable
#   Depends: libc6 (>= 2.38) but 2.35 is to be installed
#
# dnf has the identical problem: rpm compares "qt.1.fc43" against
# "qt.1.el9" as strings, so a Fedora 42 machine would be offered the
# Fedora 43 package. Neither is a packaging subtlety -- it is the whole
# point of building per distribution, undone at the last step.
#
# So: apt/dists/<codename>/ and rpm/<dist>/, each holding only what
# belongs there, and the install instructions substitute the running
# system's own name.

# --- apt, one suite per codename ---
codenames=""
for f in "$pkgdir"/*.deb; do
  ver=$(dpkg-deb -f "$f" Version)
  code=${ver##*~}
  case $code in
    "$ver") echo "cannot read a codename from $f (version $ver)"; exit 1 ;;
  esac
  mkdir -p "$out/apt/dists/$code/main/binary-amd64" "$out/apt/pool/$code"
  cp "$f" "$out/apt/pool/$code/"
  case " $codenames " in *" $code "*) ;; *) codenames="$codenames $code" ;; esac
done
for code in $codenames; do
  ( cd "$out/apt"
    apt-ftparchive packages "pool/$code" > "dists/$code/main/binary-amd64/Packages"
    gzip -9kf "dists/$code/main/binary-amd64/Packages"
    apt-ftparchive \
      -o APT::FTPArchive::Release::Origin=Astrolog \
      -o APT::FTPArchive::Release::Label=Astrolog \
      -o "APT::FTPArchive::Release::Suite=$code" \
      -o "APT::FTPArchive::Release::Codename=$code" \
      -o APT::FTPArchive::Release::Architectures=amd64 \
      -o APT::FTPArchive::Release::Components=main \
      release "dists/$code" > "dists/$code/Release" )
done

# --- dnf, one repository per dist tag ---
dists=""
for f in "$pkgdir"/*.rpm; do
  dist=$(echo "$f" | sed -n 's/.*\.\(fc[0-9][0-9]*\|el[0-9][0-9]*\)\..*/\1/p')
  [ -n "$dist" ] || { echo "cannot read a dist tag from $f"; exit 1; }
  mkdir -p "$out/rpm/$dist"
  cp "$f" "$out/rpm/$dist/"
  case " $dists " in *" $dist "*) ;; *) dists="$dists $dist" ;; esac
done

# --- rpm signing, then metadata ---
# The PACKAGES are signed, not just the metadata. dnf's gpgcheck=1 checks
# each package's own signature; repo_gpgcheck=1 checks repomd.xml. Sign
# only the metadata and a repo advertising gpgcheck=1 fails on the first
# install with "package is not signed", which is a worse first impression
# than no repository. Signing happens BEFORE createrepo_c, because it
# rewrites the .rpm and would otherwise invalidate the checksums in the
# metadata.
if [ -n "${GPG_KEY_ID:-}" ] && command -v rpmsign >/dev/null; then
  # The ABSOLUTE path to gpg. rpm execs the first word of
  # %__gpg_sign_cmd directly rather than searching PATH, so a bare "gpg"
  # gives "error: Could not exec gpg: No such file or directory" -- which
  # reads like gpg is missing when it is installed and on PATH.
  # Distributions differ here too: on Fedora %__gpg is undefined and on
  # Debian it is set, so writing "%{__gpg} gpg" expands to two words and
  # fails on Debian while working on Fedora. Resolve it here, once.
  gpgbin=$(command -v gpg)
  cat > "$HOME/.rpmmacros" <<MACROS
%_gpg_name $GPG_KEY_ID
%__gpg_sign_cmd $gpgbin --batch --pinentry-mode loopback --no-verbose --no-armor --no-secmem-warning -u "%{_gpg_name}" -sbo %{__signature_filename} %{__plaintext_filename}
MACROS
  rpmsign --addsign "$out"/rpm/*/*.rpm >/dev/null
  echo "rpm packages signed"
elif [ -n "${GPG_KEY_ID:-}" ]; then
  echo "NO rpmsign: the .rpm files would be unsigned, so a repo advertising"
  echo "gpgcheck=1 refuses them. Install rpm-sign (Fedora/EL) or rpm (Debian)."
  exit 1
fi
for dist in $dists; do
  createrepo_c --quiet "$out/rpm/$dist"
done

# --- signing the indexes ---
if [ -n "${GPG_KEY_ID:-}" ]; then
  for code in $codenames; do
    gpg --batch --yes --armor --detach-sign -u "$GPG_KEY_ID" \
        -o "$out/apt/dists/$code/Release.gpg" "$out/apt/dists/$code/Release"
    gpg --batch --yes --clearsign -u "$GPG_KEY_ID" \
        -o "$out/apt/dists/$code/InRelease" "$out/apt/dists/$code/Release"
  done
  for dist in $dists; do
    gpg --batch --yes --armor --detach-sign -u "$GPG_KEY_ID" \
        -o "$out/rpm/$dist/repodata/repomd.xml.asc" \
        "$out/rpm/$dist/repodata/repomd.xml"
  done
  # TWO EXPORTS, because the two package managers want different formats
  # and the extension is not decoration. apt's "signed-by=" reads a
  # BINARY keyring from a file named .gpg; hand it an armoured key and it
  # says "NO_PUBKEY ... is not signed", which is what the first version of
  # this script did. dnf's gpgkey= wants the ARMOURED form.
  gpg --batch --yes --export "$GPG_KEY_ID" > "$out/astrolog.gpg"
  gpg --batch --yes --armor --export "$GPG_KEY_ID" > "$out/astrolog.asc"
  echo "signed with $GPG_KEY_ID"
else
  echo "UNSIGNED: no GPG_KEY_ID. Fine for a local check, not for publishing --"
  echo "apt refuses an unsigned repository and dnf needs gpgcheck=0, which is"
  echo "a bad habit to teach."
fi

# The instructions, generated so they cannot drift from the layout above.
if [ -n "$baseurl" ]; then
  cat > "$out/index.html" <<HTML
<!doctype html><meta charset=utf-8><title>Astrolog package repository</title>
<style>body{font:14px/1.5 system-ui,sans-serif;max-width:48em;margin:3em auto;padding:0 1em}
pre{background:#f4f4f4;padding:1em;overflow-x:auto}code{background:#f4f4f4;padding:.1em .3em}</style>
<h1>Astrolog package repository</h1>
<p>The Qt/Linux port of Astrolog 8.00. Packages are built on the
distribution they target, have their dependencies computed from the
binary, and are installed into a clean container and run before they are
published.</p>

<h2>Debian / Ubuntu</h2>
<p>Suites: <code>$codenames</code></p>
<pre>sudo curl -fsSL -o /usr/share/keyrings/astrolog.gpg $baseurl/astrolog.gpg
. /etc/os-release
echo "deb [signed-by=/usr/share/keyrings/astrolog.gpg] $baseurl/apt \
  \${UBUNTU_CODENAME:-\$VERSION_CODENAME} main" \
  | sudo tee /etc/apt/sources.list.d/astrolog.list
sudo apt update &amp;&amp; sudo apt install astrolog</pre>

<h2>Fedora</h2>
<pre>sudo tee /etc/yum.repos.d/astrolog.repo &lt;&lt;'EOF'
[astrolog]
name=Astrolog
baseurl=$baseurl/rpm/fc\$releasever
enabled=1
gpgcheck=1
repo_gpgcheck=1
gpgkey=$baseurl/astrolog.asc
EOF
sudo dnf install astrolog</pre>

<h2>RHEL / Rocky / Alma</h2>
<pre>sudo tee /etc/yum.repos.d/astrolog.repo &lt;&lt;'EOF'
[astrolog]
name=Astrolog
baseurl=$baseurl/rpm/el\$releasever
enabled=1
gpgcheck=1
repo_gpgcheck=1
gpgkey=$baseurl/astrolog.asc
EOF
sudo dnf install astrolog</pre>

<p>Available: <code>$dists</code>. Each distribution gets its own suite,
because one suite holding them all makes apt and dnf offer the
highest-versioned package rather than the one built for your release.</p>
HTML
fi

echo "repo built: $ndeb deb, $nrpm rpm, in $out"
